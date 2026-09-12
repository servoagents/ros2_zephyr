#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_dir="${1:-${ROS2_ZEPHYR_CYCLONEDDS_SOURCE:-}}"
patch_file="${repository_root}/patches/cyclonedds-zephyr-4.4.patch"

if [[ ! -d "${source_dir}/.git" ]]; then
  echo "Cyclone DDS source is not a Git checkout: ${source_dir:-<unset>}" >&2
  exit 2
fi

if git -C "${source_dir}" apply --check "${patch_file}" 2>/dev/null; then
  git -C "${source_dir}" apply "${patch_file}"
elif ! git -C "${source_dir}" apply --reverse --check "${patch_file}" 2>/dev/null; then
  echo "Cyclone DDS compatibility patch does not match ${source_dir}" >&2
  exit 1
fi
