#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

sample_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd "${sample_dir}/../.." && pwd)"
ros_distro="${ROS2_ZEPHYR_ROS_DISTRO:-lyrical}"
environment_file="${ROS2_ZEPHYR_ENV_FILE:-${repository_root}/build/zephyr-env-${ros_distro}.sh}"
credentials_file="${ROS2_ZEPHYR_WIFI_ENV_FILE:-${repository_root}/build/wifi.env}"
rmw="cyclonedds_c"
rmw_source="${ROS2_ZEPHYR_RMW_SOURCE:-}"
rmw_revision_source=""
role=""
serial_device=""
reliability="best_effort"
durability="volatile"
depth="5"
output_dir=""
build_image=true
flash_image=true
reset_device=true
peer_timeout="90"
capture_timeout="180"
requested_domain_id="${ROS2_ZEPHYR_DOMAIN_ID:-}"
peer_python="${ROS2_ZEPHYR_PEER_PYTHON:-python3}"
peer_container=""
peer_address="${ROS2_ZEPHYR_PEER_ADDRESS:-}"

usage() {
  cat >&2 <<EOF
usage: $0 --role node|pub|sub|pubsub --device PATH [options]

Options:
  --rmw cyclonedds_c|zenoh_pico
  --reliability best_effort|reliable
  --durability volatile|transient_local
  --depth N
  --output-dir PATH
  --peer-timeout SECONDS
  --capture-timeout SECONDS
  --peer-container IMAGE
  --peer-address IPV4
  --no-build
  --no-flash
  --no-reset

The node and pubsub graph roles require reliable/volatile/depth-5 images.
Run this from a stock ROS 2 shell with the matching desktop RMW installed.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --rmw)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      rmw="$2"
      shift 2
      ;;
    --role)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      role="$2"
      shift 2
      ;;
    --device)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      serial_device="$2"
      shift 2
      ;;
    --reliability)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      reliability="$2"
      shift 2
      ;;
    --durability)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      durability="$2"
      shift 2
      ;;
    --depth)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      depth="$2"
      shift 2
      ;;
    --output-dir)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      output_dir="$2"
      shift 2
      ;;
    --peer-timeout)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      peer_timeout="$2"
      shift 2
      ;;
    --capture-timeout)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      capture_timeout="$2"
      shift 2
      ;;
    --peer-container)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      peer_container="$2"
      shift 2
      ;;
    --peer-address)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      peer_address="$2"
      shift 2
      ;;
    --no-build)
      build_image=false
      shift
      ;;
    --no-flash)
      flash_image=false
      shift
      ;;
    --no-reset)
      reset_device=false
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage
      exit 2
      ;;
  esac
done

if [[ "${rmw}" != "cyclonedds_c" && "${rmw}" != "zenoh_pico" ]]; then
  usage
  exit 2
fi
if [[ "${role}" != "node" && "${role}" != "pub" && "${role}" != "sub" &&
      "${role}" != "pubsub" ]]; then
  usage
  exit 2
fi
if [[ "${rmw}" == "zenoh_pico" &&
      ("${role}" == "node" || "${role}" == "pubsub") ]]; then
  echo "rmw_zenoh_pico does not provide interoperable ROS graph discovery" >&2
  exit 2
fi
if [[ -z "${serial_device}" ]]; then
  usage
  exit 2
fi
if [[ "${reliability}" != "best_effort" && "${reliability}" != "reliable" ]]; then
  usage
  exit 2
fi
if [[ "${durability}" != "volatile" && "${durability}" != "transient_local" ]]; then
  usage
  exit 2
fi
if [[ "${rmw}" == "zenoh_pico" && "${durability}" == "transient_local" ]]; then
  echo "rmw_zenoh_pico does not implement retained Transient Local history" >&2
  exit 2
fi
if [[ ! "${depth}" =~ ^[1-9][0-9]*$ ]] || ((10#${depth} > 2147483647)); then
  usage
  exit 2
fi
if [[ ! "${peer_timeout}" =~ ^[1-9][0-9]*([.][0-9]+)?$ ]] ||
   [[ ! "${capture_timeout}" =~ ^[1-9][0-9]*([.][0-9]+)?$ ]]; then
  usage
  exit 2
fi
if [[ "${role}" == "node" || "${role}" == "pubsub" ]]; then
  if [[ "${reliability}" != "reliable" || "${durability}" != "volatile" ||
        "${depth}" != "5" ]]; then
    echo "graph roles require --reliability reliable --durability volatile --depth 5" >&2
    exit 2
  fi
fi
if [[ ! -e "${serial_device}" ]]; then
  echo "serial device does not exist: ${serial_device}" >&2
  exit 2
fi
if [[ ! -f "${environment_file}" ]]; then
  echo "missing ${environment_file}; run scripts/setup.sh" >&2
  exit 2
fi
if [[ -z "${peer_address}" && -f "${credentials_file}" ]]; then
  peer_address="$(bash -c 'source "$1"; printf "%s" "${ROS_PEER_IP:-}"' \
    _ "${credentials_file}")"
fi
if [[ -n "${peer_address}" &&
      ! "${peer_address}" =~ ^[0-9]+[.][0-9]+[.][0-9]+[.][0-9]+$ ]]; then
  echo "peer address must be an IPv4 address: ${peer_address}" >&2
  exit 2
fi
peer_cyclonedds_uri=""
if [[ -n "${peer_address}" ]]; then
  peer_cyclonedds_uri="<CycloneDDS><Domain><General><Interfaces><NetworkInterface address='${peer_address}' multicast='false'/></Interfaces><AllowMulticast>false</AllowMulticast></General></Domain></CycloneDDS>"
fi
if [[ -z "${rmw_source}" && "${rmw}" == "cyclonedds_c" ]]; then
  rmw_source="${repository_root}/../rmw_cyclonedds_c"
fi
if [[ -n "${rmw_source}" ]]; then
  rmw_revision_source="${rmw_source}"
elif [[ "${rmw}" == "zenoh_pico" ]]; then
  rmw_revision_source="${repository_root}/build/deps/${ros_distro}/target/src/fj-blanco/rmw_zenoh_pico"
fi
if [[ "${build_image}" == true && -n "${rmw_source}" &&
      ! -d "${rmw_source}/rmw_${rmw}" ]]; then
  echo "ROS2_ZEPHYR_RMW_SOURCE must name the rmw_${rmw} repository" >&2
  exit 2
fi

ros2_executable=""
peer_rmw="rmw_cyclonedds_cpp"
if [[ "${rmw}" == "zenoh_pico" ]]; then
  peer_rmw="rmw_zenoh_cpp"
fi
if [[ -n "${peer_container}" ]]; then
  if ! command -v docker >/dev/null; then
    echo "docker is required by --peer-container" >&2
    exit 2
  fi
  if ! docker run --rm --network host -e RMW_IMPLEMENTATION="${peer_rmw}" \
      "${peer_container}" \
      python3 -c 'import rclpy, std_msgs' 2>/dev/null; then
    echo "${peer_container} cannot import stock ROS 2 rclpy/std_msgs" >&2
    exit 2
  fi
else
  peer_python="$(command -v "${peer_python}")"
  if ! "${peer_python}" -c 'import rclpy, std_msgs' 2>/dev/null; then
    echo "${peer_python} cannot import stock ROS 2 rclpy/std_msgs; run from a ROS shell" >&2
    exit 2
  fi
  if [[ "${role}" == "pubsub" || "${rmw}" == "zenoh_pico" ]]; then
    ros2_executable="$(command -v ros2 || true)"
    if [[ -z "${ros2_executable}" ]]; then
      echo "ros2 CLI is required for the Zenoh router or outbound graph snapshot" >&2
      exit 2
    fi
  fi
fi

profile="${rmw}-${role}-${reliability}-${durability}-depth-${depth}"
build_dir="${repository_root}/build/${ros_distro}/wifi-esp32s3-${profile}"
build_dir="${ROS2_ZEPHYR_WIFI_BUILD_DIR:-${build_dir}}"
output_dir="${output_dir:-${repository_root}/results/${ros_distro}/esp32s3-hardware/${profile}}"
mkdir -p "${output_dir}"

build_log="${output_dir}/build.log"
flash_log="${output_dir}/flash.log"
peer_log="${output_dir}/peer.log"
device_log="${output_dir}/device.log"
cli_log="${output_dir}/ros2-cli.log"
summary_log="${output_dir}/summary.log"
: >"${summary_log}"

source_revision() {
  local source_dir="$1"
  local revision

  if [[ -z "${source_dir}" || ! -d "${source_dir}" ]]; then
    printf unknown
    return
  fi
  revision="$(git -C "${source_dir}" rev-parse HEAD 2>/dev/null || printf unknown)"
  if [[ "${revision}" != "unknown" ]] &&
     [[ -n "$(git -C "${source_dir}" status --short --untracked-files=no 2>/dev/null)" ]]; then
    revision="${revision}-dirty"
  fi
  printf '%s' "${revision}"
}

zephyr_source="$(bash -c 'source "$1"; printf "%s" "${ZEPHYR_BASE:-}"' \
  _ "${environment_file}")"
printf 'SOURCE ros2_zephyr=%s rmw=%s rmw_revision=%s zephyr=%s\n' \
  "$(source_revision "${repository_root}")" \
  "${rmw}" \
  "$(source_revision "${rmw_revision_source}")" \
  "$(source_revision "${zephyr_source}")" | tee -a "${summary_log}"

if [[ "${build_image}" == true ]]; then
  echo "BUILD role=${role} reliability=${reliability} durability=${durability} depth=${depth}" |
    tee -a "${summary_log}"
  ROS2_ZEPHYR_RMW_SOURCE="${rmw_source}" \
    ROS2_ZEPHYR_ZENOH_ROUTER_IPV4="${peer_address}" \
    ROS2_ZEPHYR_WIFI_BUILD_DIR="${build_dir}" \
    "${sample_dir}/build_esp32.sh" --rmw "${rmw}" --role "${role}" \
    --reliability "${reliability}" \
    --durability "${durability}" --depth "${depth}" 2>&1 | tee "${build_log}"
  linked_flash="$(awk '$1 == "FLASH:" {value = $2} END {print value}' "${build_log}")"
  linked_dram="$(awk '$1 == "dram0_0_seg:" {value = $2} END {print value}' "${build_log}")"
  if [[ -z "${linked_flash}" || -z "${linked_dram}" ]]; then
    echo "missing linked-memory totals in ${build_log}" >&2
    exit 1
  fi
  printf 'BUILD_MEMORY linked_flash=%s linked_dram=%s\n' \
    "${linked_flash}" "${linked_dram}" | tee -a "${summary_log}"
elif [[ ! -f "${build_dir}/zephyr/zephyr.elf" ]]; then
  echo "missing ${build_dir}/zephyr/zephyr.elf; remove --no-build" >&2
  exit 2
fi

domain_id="$(awk -F= '$1 == "CONFIG_ROS2_ZEPHYR_DOMAIN_ID" {print $2}' \
  "${build_dir}/zephyr/.config")"
if [[ ! "${domain_id}" =~ ^[0-9]+$ ]]; then
  echo "cannot read the firmware ROS domain from ${build_dir}/zephyr/.config" >&2
  exit 1
fi
if [[ -n "${requested_domain_id}" && "${requested_domain_id}" != "${domain_id}" ]]; then
  echo "ROS2_ZEPHYR_DOMAIN_ID=${requested_domain_id} does not match firmware domain ${domain_id}" >&2
  exit 2
fi
if [[ "${rmw}" == "zenoh_pico" ]]; then
  router_ipv4="$(sed -n \
    's/^ROS2_ZEPHYR_ZENOH_ROUTER_IPV4:STRING=//p' "${build_dir}/CMakeCache.txt")"
  router_port="$(sed -n \
    's/^ROS2_ZEPHYR_ZENOH_ROUTER_PORT:STRING=//p' "${build_dir}/CMakeCache.txt")"
  if [[ -n "${peer_address}" && "${router_ipv4}" != "${peer_address}" ]]; then
    echo "firmware router address ${router_ipv4:-unknown} does not match ${peer_address}" >&2
    exit 2
  fi
  if [[ "${router_port}" != "7447" ]]; then
    echo "the managed Zenoh router requires firmware port 7447; found ${router_port:-unknown}" >&2
    exit 2
  fi
fi

case "${role}" in
  node)
    peer_role="graph-inbound"
    ;;
  pubsub)
    peer_role="graph-outbound"
    ;;
  pub)
    peer_role="sub"
    ;;
  sub)
    peer_role="pub"
    ;;
esac

peer_args=("${peer_role}" --timeout "${peer_timeout}")
if [[ "${role}" == "pub" || "${role}" == "sub" ]]; then
  peer_args+=(--reliability "${reliability}" --durability "${durability}" --depth "${depth}")
elif [[ "${role}" == "pubsub" ]]; then
  peer_args+=(--cycles 2)
fi
defer_peer=false
if [[ "${rmw}" == "zenoh_pico" && "${role}" == "sub" ]]; then
  peer_args+=(--skip-match)
  defer_peer=true
fi

peer_pid=""
capture_pid=""
peer_container_name=""
router_pid=""
router_container_name=""
cleanup_processes() {
  local pid
  for pid in "${capture_pid}" "${peer_pid}" "${router_pid}"; do
    if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
      kill -TERM "${pid}" 2>/dev/null || true
    fi
  done
  if [[ -n "${peer_container_name}" ]]; then
    docker rm -f "${peer_container_name}" >/dev/null 2>&1 || true
  fi
  if [[ -n "${router_container_name}" ]]; then
    docker rm -f "${router_container_name}" >/dev/null 2>&1 || true
  fi
  for pid in "${capture_pid}" "${peer_pid}" "${router_pid}"; do
    if [[ -n "${pid}" ]]; then
      wait "${pid}" 2>/dev/null || true
    fi
  done
}
trap cleanup_processes EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

router_log="${output_dir}/router.log"
if [[ "${rmw}" == "zenoh_pico" ]]; then
  if [[ -n "${peer_container}" ]]; then
    router_container_name="ros2-zephyr-router-$$"
    docker run --rm --name "${router_container_name}" --network host \
      "${peer_container}" ros2 run rmw_zenoh_cpp rmw_zenohd \
      >"${router_log}" 2>&1 &
    router_pid=$!
  else
    env RMW_IMPLEMENTATION="${peer_rmw}" \
      "${ros2_executable}" run rmw_zenoh_cpp rmw_zenohd \
      >"${router_log}" 2>&1 &
    router_pid=$!
  fi
  sleep 2
  if ! kill -0 "${router_pid}" 2>/dev/null; then
    cat "${router_log}" >&2
    echo "Zenoh router did not start" >&2
    exit 1
  fi
fi

start_peer() {
  if [[ -n "${peer_container}" ]]; then
    peer_container_name="ros2-zephyr-peer-$$"
    docker run --rm --name "${peer_container_name}" --network host \
      -v "${repository_root}:/workspace:ro" -w /workspace \
      -e ROS_DOMAIN_ID="${domain_id}" -e RMW_IMPLEMENTATION="${peer_rmw}" \
      -e PYTHONUNBUFFERED=1 -e CYCLONEDDS_URI="${peer_cyclonedds_uri}" \
      "${peer_container}" \
      python3 samples/wifi/peer.py "${peer_args[@]}" >"${peer_log}" 2>&1 &
  else
    env ROS_DOMAIN_ID="${domain_id}" RMW_IMPLEMENTATION="${peer_rmw}" PYTHONUNBUFFERED=1 \
      CYCLONEDDS_URI="${peer_cyclonedds_uri}" \
      "${peer_python}" "${sample_dir}/peer.py" "${peer_args[@]}" >"${peer_log}" 2>&1 &
  fi
  peer_pid=$!
}

if [[ "${defer_peer}" == false ]]; then
  start_peer
fi

if [[ "${flash_image}" == true ]]; then
  set +e
  ROS2_ZEPHYR_WIFI_BUILD_DIR="${build_dir}" \
    "${sample_dir}/run_esp32.sh" --rmw "${rmw}" --role "${role}" \
    --device "${serial_device}" \
    --reliability "${reliability}" --durability "${durability}" --depth "${depth}" \
    --no-build >"${flash_log}" 2>&1
  flash_status=$?
  set -e
  if [[ "${flash_status}" -ne 0 ]]; then
    cat "${flash_log}" >&2
    echo "flash failed (status=${flash_status})" >&2
    exit 1
  fi
else
  flash_status=0
  echo 'flash skipped; using image already installed on the board' >"${flash_log}"
fi

# The Zephyr virtual environment supplies pyserial and esp-pylib. The already
# running desktop peer keeps the stock ROS Python selected above.
# shellcheck disable=SC1090
source "${environment_file}"
capture_python="${ROS2_ZEPHYR_CAPTURE_PYTHON:-$(command -v python3)}"
capture_args=("${serial_device}" --timeout "${capture_timeout}" \
  --until 'ROS2_ZEPHYR_CLEANUP status=0 ros_live=0 middleware_live=0' --reconnect)
if [[ "${reset_device}" == false ]]; then
  capture_args+=(--no-reset)
fi
"${capture_python}" "${sample_dir}/../loopback/capture_esp32.py" \
  "${capture_args[@]}" >"${device_log}" 2>&1 &
capture_pid=$!

if [[ "${defer_peer}" == true ]]; then
  ready_deadline=$((SECONDS + 60))
  while ! grep -Fq 'ROS2_ZEPHYR_READY role=sub ' "${device_log}"; do
    if ! kill -0 "${capture_pid}" 2>/dev/null || ((SECONDS >= ready_deadline)); then
      echo "device subscriber was not ready in time" >"${peer_log}"
      exit 1
    fi
    sleep 0.2
  done
  start_peer
fi

cli_status=0
if [[ "${role}" == "pubsub" ]]; then
  phase_deadline=$((SECONDS + 60))
  while ! grep -Fq \
    'ROS2_ZEPHYR_PEER_GRAPH_PASS direction=outbound phase=pubsub' "${peer_log}"; do
    if ! kill -0 "${peer_pid}" 2>/dev/null || ((SECONDS >= phase_deadline)); then
      cli_status=1
      break
    fi
    sleep 0.2
  done
  if [[ "${cli_status}" -eq 0 ]]; then
    device_address="$(sed -n \
      's/.*ROS2_ZEPHYR_WIFI_READY .*address=\([0-9][0-9.]*\).*/\1/p' \
      "${device_log}" | tail -n 1)"
    if [[ ! "${device_address}" =~ ^[0-9]+[.][0-9]+[.][0-9]+[.][0-9]+$ ]]; then
      echo "device IPv4 address was not observed in time" >"${cli_log}"
      cli_status=1
    fi
  fi
  if [[ "${cli_status}" -eq 0 ]]; then
    cli_interface_xml=""
    if [[ -n "${peer_address}" ]]; then
      cli_interface_xml="<Interfaces><NetworkInterface address='${peer_address}' multicast='false'/></Interfaces><AllowMulticast>false</AllowMulticast>"
    fi
    cli_cyclonedds_uri="<CycloneDDS><Domain><General>${cli_interface_xml}</General><Discovery><ParticipantIndex>1</ParticipantIndex><MaxAutoParticipantIndex>0</MaxAutoParticipantIndex><Peers><Peer Address='${device_address}'/></Peers></Discovery></Domain></CycloneDDS>"
    set +e
    if [[ -n "${peer_container}" ]]; then
      ros2_command=(docker run --rm --network host \
        -e ROS_DOMAIN_ID="${domain_id}" -e RMW_IMPLEMENTATION="${peer_rmw}" \
        -e CYCLONEDDS_URI="${cli_cyclonedds_uri}" "${peer_container}" ros2)
    else
      ros2_command=(env ROS_DOMAIN_ID="${domain_id}" RMW_IMPLEMENTATION="${peer_rmw}" \
        CYCLONEDDS_URI="${cli_cyclonedds_uri}" "${ros2_executable}")
    fi
    {
      echo '$ ros2 node list --no-daemon'
      timeout 15 "${ros2_command[@]}" node list --no-daemon &&
        echo '$ ros2 node info /ros2_zephyr_esp32s3 --no-daemon' &&
        timeout 15 "${ros2_command[@]}" node info /ros2_zephyr_esp32s3 --no-daemon &&
        echo '$ ros2 topic list --no-daemon' &&
        timeout 15 "${ros2_command[@]}" topic list --no-daemon &&
        echo '$ ros2 topic info /ros2_zephyr/graph_local --verbose --no-daemon' &&
        timeout 15 "${ros2_command[@]}" topic info /ros2_zephyr/graph_local \
          --verbose --no-daemon
    } >"${cli_log}" 2>&1
    cli_status=$?
    set -e
  else
    echo "outbound pubsub phase was not observed in time" >"${cli_log}"
  fi
fi

set +e
wait "${capture_pid}"
capture_status=$?
capture_pid=""
if [[ "${capture_status}" -eq 0 && "${role}" == "pubsub" ]]; then
  echo '--- ESP32-S3 restart ---' >>"${device_log}"
  "${capture_python}" "${sample_dir}/../loopback/capture_esp32.py" \
    "${capture_args[@]}" >>"${device_log}" 2>&1 &
  capture_pid=$!
  wait "${capture_pid}"
  capture_status=$?
  capture_pid=""
fi
if [[ "${capture_status}" -ne 0 ]] && kill -0 "${peer_pid}" 2>/dev/null; then
  kill -TERM "${peer_pid}" 2>/dev/null
fi
wait "${peer_pid}"
peer_status=$?
peer_pid=""
set -e

case "${role}" in
  node)
    device_markers=(
      'ROS2_ZEPHYR_GRAPH_PASS phase=initial '
      'ROS2_ZEPHYR_GRAPH_PASS phase=reduced '
      'ROS2_ZEPHYR_GRAPH_PASS phase=participant_lost '
      'ROS2_ZEPHYR_GRAPH_PASS phase=restart '
      'ROS2_ZEPHYR_GRAPH_PASS phase=restart_lost '
      'ROS2_ZEPHYR_GRAPH_CACHE_EVIDENCE participants=3 nodes=3 endpoints=5 source=validated_topology'
    )
    peer_markers=('ROS2_ZEPHYR_PEER_PASS role=graph-inbound')
    ;;
  pubsub)
    device_markers=(
      'ROS2_ZEPHYR_GRAPH_LOCAL phase=pubsub'
      'ROS2_ZEPHYR_GRAPH_LOCAL phase=subscription_only'
      'ROS2_ZEPHYR_GRAPH_LOCAL phase=node_only'
    )
    peer_markers=(
      'ROS2_ZEPHYR_PEER_GRAPH_PASS direction=outbound phase=pubsub'
      'ROS2_ZEPHYR_PEER_GRAPH_PASS direction=outbound phase=subscription_only'
      'ROS2_ZEPHYR_PEER_GRAPH_PASS direction=outbound phase=node_only'
      'ROS2_ZEPHYR_PEER_GRAPH_PASS direction=outbound phase=node_cleanup'
    )
    ;;
  pub)
    device_markers=("ROS2_ZEPHYR_QOS role=pub reliability=${reliability} durability=${durability} depth=${depth}")
    peer_markers=("ROS2_ZEPHYR_PEER_PASS role=sub reliability=${reliability} durability=${durability} depth=${depth}")
    ;;
  sub)
    device_markers=("ROS2_ZEPHYR_QOS role=sub reliability=${reliability} durability=${durability} depth=${depth}")
    peer_markers=("ROS2_ZEPHYR_PEER_PASS role=pub reliability=${reliability} durability=${durability} depth=${depth}")
    ;;
esac

failed=false
if [[ "${capture_status}" -ne 0 || "${peer_status}" -ne 0 || "${cli_status}" -ne 0 ]]; then
  failed=true
fi
for marker in \
  'ROS2_ZEPHYR_ALLOC ' \
  'ROS2_ZEPHYR_STACK_TOTAL ' \
  'ROS2_ZEPHYR_CLEANUP status=0 ros_live=0 middleware_live=0' \
  "${device_markers[@]}"; do
  if ! grep -Fq "${marker}" "${device_log}"; then
    echo "missing device marker: ${marker}" | tee -a "${summary_log}" >&2
    failed=true
  fi
done
for marker in "${peer_markers[@]}"; do
  if ! grep -Fq "${marker}" "${peer_log}"; then
    echo "missing peer marker: ${marker}" | tee -a "${summary_log}" >&2
    failed=true
  fi
done
if grep -Fq 'ROS2_ZEPHYR_ERROR ' "${device_log}" ||
   grep -Fq 'ROS2_ZEPHYR_PEER_ERROR ' "${peer_log}"; then
  echo "an error marker was emitted" | tee -a "${summary_log}" >&2
  failed=true
fi
if [[ "${role}" == "pubsub" ]]; then
  if ! grep -Fq '/ros2_zephyr_esp32s3' "${cli_log}" ||
     ! grep -Fq '/ros2_zephyr/graph_local' "${cli_log}"; then
    echo "ROS CLI snapshot did not contain the device node and graph topic" |
      tee -a "${summary_log}" >&2
    failed=true
  fi
  for marker in \
    'ROS2_ZEPHYR_GRAPH_LOCAL phase=pubsub' \
    'ROS2_ZEPHYR_GRAPH_LOCAL phase=subscription_only' \
    'ROS2_ZEPHYR_GRAPH_LOCAL phase=node_only' \
    'ROS2_ZEPHYR_CLEANUP status=0 ros_live=0 middleware_live=0'; do
    marker_count="$(grep -Fc "${marker}" "${device_log}" || true)"
    if [[ "${marker_count}" -ne 2 ]]; then
      echo "restart evidence does not contain two device markers: ${marker}" |
        tee -a "${summary_log}" >&2
      failed=true
    fi
  done
  for cycle in 1 2; do
    if ! grep -Fq \
      "ROS2_ZEPHYR_PEER_GRAPH_PASS direction=outbound phase=node_cleanup cycle=${cycle}" \
      "${peer_log}"; then
      echo "missing outbound cleanup evidence for cycle ${cycle}" |
        tee -a "${summary_log}" >&2
      failed=true
    fi
  done
fi

printf 'STATUS flash=%d capture=%d peer=%d cli=%d\n' \
  "${flash_status}" "${capture_status}" "${peer_status}" "${cli_status}" |
  tee -a "${summary_log}"
if [[ "${failed}" == true ]]; then
  echo "FAIL role=${role} reliability=${reliability} durability=${durability} depth=${depth}" |
    tee -a "${summary_log}" >&2
  echo "logs: ${output_dir}" >&2
  exit 1
fi

echo "PASS role=${role} reliability=${reliability} durability=${durability} depth=${depth}" |
  tee -a "${summary_log}"
echo "logs: ${output_dir}"
