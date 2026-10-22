#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_dir="${1:?Zephyr source directory required}"
expected=e91e655557e622f052d091f18f9570b50e4c5a63
[[ "$(git -C "$source_dir" rev-parse HEAD)" == "$expected" ]] || {
  echo "The C6 TLS patch requires Zephyr $expected" >&2
  exit 1
}
patch="$root/patches/zephyr-riscv-tls-alignment.patch"
if git -C "$source_dir" apply --check "$patch" 2>/dev/null; then
  git -C "$source_dir" apply "$patch"
else
  git -C "$source_dir" apply --reverse --check "$patch"
fi
