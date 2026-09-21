#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

sample_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
image="${ROS2_ZEPHYR_ZENOH_PEER_IMAGE:-ros2-zephyr-zenoh-peer:jazzy-0.2.5}"
revision="aae224e449f8f364f4a8025fe85899ce06f5381b"

docker build \
  --build-arg "RMW_ZENOH_REVISION=${revision}" \
  --tag "${image}" \
  --file "${sample_dir}/Dockerfile.zenoh-peer" \
  "${sample_dir}"

printf '%s\n' "${image}"
