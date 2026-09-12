#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

git -C "${repository_root}" diff --check

while IFS= read -r -d '' script; do
  bash -n "${script}"
done < <(find "${repository_root}" -path "${repository_root}/.git" -prune \
  -o -path "${repository_root}/build" -prune \
  -o -type f -name '*.sh' -print0)

while IFS= read -r -d '' source; do
  python3 -m py_compile "${source}"
done < <(find "${repository_root}" -path "${repository_root}/.git" -prune \
  -o -path "${repository_root}/build" -prune \
  -o -type f -name '*.py' -print0)

if command -v shellcheck >/dev/null; then
  find "${repository_root}" -path "${repository_root}/.git" -prune \
    -o -path "${repository_root}/build" -prune \
    -o -type f -name '*.sh' -print0 | xargs -0 shellcheck
else
  echo "warning: shellcheck is not installed; skipped shell lint" >&2
fi
