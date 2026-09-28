#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
zephyr_source="${1:-${ROS2_ZEPHYR_WORKSPACE:-${repository_root}/build/zephyr-4.4.0}/zephyr}"
patch="${repository_root}/patches/zephyr-condvar-timeout-relock.patch"
expected_commit="684c9e8f32e4373a21098559f748f06915f950c9"

if [[ "$(git -C "${zephyr_source}" rev-parse HEAD)" != "${expected_commit}" ]]; then
  echo "Zephyr condition-variable patch is qualified only for ${expected_commit}" >&2
  exit 1
fi
if git -C "${zephyr_source}" apply --reverse --check "${patch}" 2>/dev/null; then
  echo "Zephyr condition-variable fix already applied"
elif git -C "${zephyr_source}" apply --check "${patch}" 2>/dev/null; then
  git -C "${zephyr_source}" apply "${patch}"
  echo "Applied Zephyr condition-variable fix"
else
  echo "Zephyr condition-variable patch conflicts with local changes" >&2
  exit 1
fi
