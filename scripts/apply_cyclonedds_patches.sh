#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_dir="${1:-${ROS2_ZEPHYR_CYCLONEDDS_SOURCE:-}}"
if [[ ! -e "${source_dir}/.git" ]]; then
  echo "Cyclone DDS source is not a Git checkout: ${source_dir:-<unset>}" >&2
  exit 2
fi

for patch in cyclonedds-zephyr-4.4 cyclonedds-log-buffer; do
  patch_file="${repository_root}/patches/${patch}.patch"
  if git -C "${source_dir}" apply --check "${patch_file}" 2>/dev/null; then
    git -C "${source_dir}" apply "${patch_file}"
  elif ! git -C "${source_dir}" apply --reverse --check "${patch_file}" 2>/dev/null; then
    echo "Cyclone DDS patch ${patch} does not match ${source_dir}" >&2
    exit 1
  fi
done
