#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

module_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ros_distro="${ROS2_ZEPHYR_ROS_DISTRO:-lyrical}"
deps_root="${ROS2_ZEPHYR_DEPS_ROOT:-${module_dir}/build/deps/${ros_distro}}"

for command in git vcs colcon; do
  command -v "${command}" >/dev/null || {
    echo "missing host command: ${command}" >&2
    echo "run scripts/setup.sh first" >&2
    exit 2
  }
done

import_group() {
  local group="$1"
  local manifest="${module_dir}/dependencies/${group}-${ros_distro}.repos"
  local destination="${deps_root}/${group}/src"

  if [[ ! -f "${manifest}" ]]; then
    manifest="${module_dir}/dependencies/${group}.repos"
  fi

  mkdir -p "${destination}"
  if ! "${module_dir}/verify_sources.py" "${manifest}" "${destination}" \
    >/dev/null 2>&1; then
    vcs import --recursive "${destination}" <"${manifest}"
  fi
  "${module_dir}/verify_sources.py" "${manifest}" "${destination}"
}

import_group host
import_group target
import_group platform

echo "Pinned sources are ready under ${deps_root}"
