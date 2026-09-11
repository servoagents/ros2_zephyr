#!/usr/bin/env bash
set -euo pipefail

MODULE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LAB_ROOT="$(cd "${MODULE_DIR}/../../.." && pwd)"
DEPS_ROOT="${PHASE5_DEPS_ROOT:-${LAB_ROOT}/build/phase5/deps}"

for command in git vcs colcon; do
  command -v "${command}" >/dev/null || {
    echo "missing host command: ${command}" >&2
    echo "run scripts/phase5.sh setup-tools" >&2
    exit 2
  }
done

mkdir -p "${DEPS_ROOT}/host/src" "${DEPS_ROOT}/target/src"
if ! "${MODULE_DIR}/verify_sources.py" \
  "${MODULE_DIR}/dependencies/host-tools.repos" "${DEPS_ROOT}/host/src" \
  >/dev/null 2>&1; then
  vcs import --recursive "${DEPS_ROOT}/host/src" \
    < "${MODULE_DIR}/dependencies/host-tools.repos"
fi
if ! "${MODULE_DIR}/verify_sources.py" \
  "${MODULE_DIR}/dependencies/target.repos" "${DEPS_ROOT}/target/src" \
  >/dev/null 2>&1; then
  vcs import --recursive "${DEPS_ROOT}/target/src" \
    < "${MODULE_DIR}/dependencies/target.repos"
fi

"${MODULE_DIR}/verify_sources.py" \
  "${MODULE_DIR}/dependencies/host-tools.repos" "${DEPS_ROOT}/host/src"
"${MODULE_DIR}/verify_sources.py" \
  "${MODULE_DIR}/dependencies/target.repos" "${DEPS_ROOT}/target/src"

echo "Pinned Phase 5 sources are ready under ${DEPS_ROOT}"
