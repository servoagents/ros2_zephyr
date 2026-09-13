#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

zephyr_version="v4.4.0"
sdk_version="1.0.1"
sdk_toolchains=(
  xtensa-espressif_esp32_zephyr-elf
  xtensa-espressif_esp32s3_zephyr-elf
)
ros_distro="${ROS2_ZEPHYR_ROS_DISTRO:-lyrical}"

case "${ros_distro}" in
lyrical | kilted) ;;
*)
  echo "unsupported ROS 2 distribution: ${ros_distro}" >&2
  exit 2
  ;;
esac

workspace="${ROS2_ZEPHYR_WORKSPACE:-${repository_root}/build/zephyr-${zephyr_version#v}}"
venv="${ROS2_ZEPHYR_VENV:-${repository_root}/build/venv}"
sdk_dir="${ZEPHYR_SDK_INSTALL_DIR:-${repository_root}/build/zephyr-sdk-${sdk_version}}"
deps_root="${ROS2_ZEPHYR_DEPS_ROOT:-${repository_root}/build/deps/${ros_distro}}"
host_build="${repository_root}/build/host/${ros_distro}/cyclonedds-build"
host_install="${repository_root}/build/host/${ros_distro}/cyclonedds-install"
environment_file="${repository_root}/build/zephyr-env-${ros_distro}.sh"
host_manifest="${repository_root}/dependencies/host-${ros_distro}.repos"
target_manifest="${repository_root}/dependencies/target-${ros_distro}.repos"

[[ -f "${host_manifest}" ]] || host_manifest="${repository_root}/dependencies/host.repos"
[[ -f "${target_manifest}" ]] || target_manifest="${repository_root}/dependencies/target.repos"

for command in git cmake ninja python3; do
  command -v "${command}" >/dev/null || {
    echo "missing host dependency: ${command}" >&2
    exit 1
  }
done

python3 - <<'PY'
import sys

if sys.version_info < (3, 12):
    raise SystemExit("Python 3.12 or newer is required for Zephyr 4.4")
PY

if [[ ! -d "${venv}" ]]; then
  python3 -m venv "${venv}"
fi

# shellcheck disable=SC1091
source "${venv}/bin/activate"
python -m pip install --upgrade pip west

if [[ ! -d "${workspace}/.west" ]]; then
  if [[ -d "${workspace}" ]] &&
    find "${workspace}" -mindepth 1 -maxdepth 1 -print -quit | grep -q .; then
    echo "refusing to replace non-west directory: ${workspace}" >&2
    exit 1
  fi
  west init -m https://github.com/zephyrproject-rtos/zephyr \
    --mr "${zephyr_version}" "${workspace}"
fi

cd "${workspace}"
actual_version="$(git -C zephyr describe --tags --exact-match 2>/dev/null || true)"
if [[ "${actual_version}" != "${zephyr_version}" ]]; then
  echo "expected Zephyr ${zephyr_version}, found ${actual_version:-an untagged revision}" >&2
  exit 1
fi

west update --narrow hal_espressif mbedtls picolibc tf-psa-crypto
python -m pip install \
  -r zephyr/scripts/requirements-base.txt \
  -r modules/hal/espressif/zephyr/requirements.txt \
  catkin-pkg \
  colcon-common-extensions \
  'empy==3.3.4' \
  lark \
  vcstool
west blobs fetch hal_espressif

sdk_complete=1
for sdk_toolchain in "${sdk_toolchains[@]}"; do
  if [[ ! -x "${sdk_dir}/gnu/${sdk_toolchain}/bin/${sdk_toolchain}-gcc" ]]; then
    sdk_complete=0
  fi
done

if [[ "${ROS2_ZEPHYR_SKIP_SDK:-0}" != "1" && "${sdk_complete}" != "1" ]]; then
  west sdk install --version "${sdk_version}" --install-dir "${sdk_dir}" \
    --gnu-toolchains "${sdk_toolchains[@]}"
fi

cd "${repository_root}"
ROS2_ZEPHYR_DEPS_ROOT="${deps_root}" ROS2_ZEPHYR_ROS_DISTRO="${ros_distro}" \
  ./prepare_sources.sh

cyclonedds_source="${deps_root}/platform/src/eclipse/cyclonedds"
"${repository_root}/scripts/apply_cyclonedds_patches.sh" "${cyclonedds_source}"
cmake -S "${cyclonedds_source}" -B "${host_build}" -G Ninja \
  -DBUILD_EXAMPLES=OFF \
  -DBUILD_TESTING=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${host_install}"
cmake --build "${host_build}" --parallel "${ROS2_ZEPHYR_BUILD_JOBS:-1}"
cmake --install "${host_build}"

mkdir -p "$(dirname "${environment_file}")"
{
  printf 'source %q\n' "${venv}/bin/activate"
  printf 'export ROS2_ZEPHYR_WORKSPACE=%q\n' "${workspace}"
  printf 'export ROS2_ZEPHYR_ROS_DISTRO=%q\n' "${ros_distro}"
  printf 'export ROS2_ZEPHYR_HOST_MANIFEST=%q\n' "${host_manifest}"
  printf 'export ROS2_ZEPHYR_TARGET_MANIFEST=%q\n' "${target_manifest}"
  printf 'export ZEPHYR_BASE=%q\n' "${workspace}/zephyr"
  printf 'export ZEPHYR_SDK_INSTALL_DIR=%q\n' "${sdk_dir}"
  printf 'export ZEPHYR_TOOLCHAIN_VARIANT=zephyr\n'
  printf 'export ROS2_ZEPHYR_BOARD=esp32_devkitc/esp32/procpu\n'
  printf 'export ROS2_ZEPHYR_DEPS_ROOT=%q\n' "${deps_root}"
  printf 'export ROS2_ZEPHYR_CYCLONEDDS_SOURCE=%q\n' "${cyclonedds_source}"
  printf 'export ROS2_ZEPHYR_HOST_IDLC=%q\n' "${host_install}/bin/idlc"
  printf 'export ROS2_ZEPHYR_MODULES=%q\n' \
    "${workspace}/modules/lib/picolibc;${workspace}/modules/hal/espressif;${workspace}/modules/crypto/mbedtls;${workspace}/modules/crypto/tf-psa-crypto"
} >"${environment_file}"

echo "Setup complete for ROS 2 ${ros_distro}."
echo "Use ROS2_ZEPHYR_ENV_FILE=${environment_file} scripts/run.sh native"
