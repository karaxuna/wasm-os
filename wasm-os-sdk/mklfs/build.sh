#!/usr/bin/env bash
# Compile mklfs.c + littlefs to ../assets/mklfs.wasm with wasi-sdk (fetched
# via wasm-os-tests/scripts on first run). The artifact is committed, so this
# only needs re-running when mklfs.c changes or littlefs is bumped.
#
# LITTLEFS_VERSION must match the lfs version vendored by the firmware's
# joltwallet__littlefs component (LFS_VERSION in its lfs.h).
set -euo pipefail

LITTLEFS_VERSION=2.11.0

MKLFS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS_DIR="${MKLFS_DIR}/../../wasm-os-tests/scripts"
LFS_DIR="${MKLFS_DIR}/littlefs"
OUT="${MKLFS_DIR}/../assets/mklfs.wasm"

WASI_SDK="$("${SCRIPTS_DIR}/fetch-wasi-sdk.sh")"
CLANG="${WASI_SDK}/bin/clang"

if [ ! -f "${LFS_DIR}/lfs.c" ]; then
  echo "Fetching littlefs ${LITTLEFS_VERSION}..."
  mkdir -p "${LFS_DIR}"
  curl -fLsS --retry 3 --retry-delay 2 \
    "https://github.com/littlefs-project/littlefs/archive/refs/tags/v${LITTLEFS_VERSION}.tar.gz" |
    tar -xz -C "${LFS_DIR}" --strip-components=1
fi

mkdir -p "$(dirname "${OUT}")"

"${CLANG}" \
  --target=wasm32-wasi \
  -Oz \
  -DLFS_NO_MALLOC -DLFS_NO_ASSERT -DLFS_NO_DEBUG -DLFS_NO_WARN -DLFS_NO_ERROR \
  -I"${LFS_DIR}" \
  -mexec-model=reactor \
  -Wl,--no-entry -Wl,--strip-all \
  "${MKLFS_DIR}/mklfs.c" "${LFS_DIR}/lfs.c" "${LFS_DIR}/lfs_util.c" \
  -o "${OUT}"

echo "Built ${OUT} ($(stat -f%z "${OUT}" 2>/dev/null || stat -c%s "${OUT}") bytes)"
