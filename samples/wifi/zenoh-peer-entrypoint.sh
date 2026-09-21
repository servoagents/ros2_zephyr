#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -e

# shellcheck disable=SC1091
source /opt/ros/jazzy/setup.bash
# shellcheck disable=SC1091
source /opt/rmw_zenoh/install/local_setup.bash
exec "$@"
