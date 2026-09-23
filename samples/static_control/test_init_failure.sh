#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

sample_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd "${sample_dir}/../.." && pwd)"
build_dir="${repository_root}/build/lyrical/static-control-init-failure"

export ROS2_ZEPHYR_STATIC_CONTROL_BUILD_DIR="${build_dir}"
export ROS2_ZEPHYR_STATIC_CONTROL_EXTRA_CONF_FILE="${sample_dir}/prj_init_failure.conf"
"${sample_dir}/build_native.sh"

set +e
output="$(timeout 5 "${build_dir}/zephyr/zephyr.exe" 2>&1)"
status=$?
set -e
printf '%s\n' "${output}"

if ((status != 124)); then
  echo "native simulator exited unexpectedly with status ${status}" >&2
  exit 1
fi
if ! grep -Fq "ROS2_ZEPHYR_ALLOC_FAILURE domain=ros" <<<"${output}"; then
  echo "missing injected allocation failure" >&2
  exit 1
fi
if ! grep -Fq "STATIC_CONTROL_CLEANUP result=1 ros_live=0 middleware_live=0" <<<"${output}"; then
  echo "initialization failure did not release all tracked allocations" >&2
  exit 1
fi
if grep -Fq "STATIC_CONTROL_READY" <<<"${output}"; then
  echo "application work started after initialization failure" >&2
  exit 1
fi

echo "PASS static control partial-initialization cleanup"
