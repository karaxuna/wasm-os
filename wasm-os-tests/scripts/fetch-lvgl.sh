#!/usr/bin/env bash
# Download a pinned LVGL release into wasm-os-tests/toolchain/lvgl (gitignored).
# Skipped when LVGL_PATH is set or the source is already present.
# Prints the source path on success.
set -euo pipefail

LVGL_VERSION="9.2.2"

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${TESTS_DIR}/toolchain/lvgl"

if [ -n "${LVGL_PATH:-}" ]; then
  echo "${LVGL_PATH}"
  exit 0
fi

if [ -f "${DEST}/lvgl.h" ]; then
  echo "${DEST}"
  exit 0
fi

ARCHIVE="lvgl-${LVGL_VERSION}.tar.gz"
URL="https://codeload.github.com/lvgl/lvgl/tar.gz/refs/tags/v${LVGL_VERSION}"

echo "Downloading LVGL ${LVGL_VERSION}..." >&2
mkdir -p "${TESTS_DIR}/toolchain"
curl -fLsS --retry 3 --retry-delay 2 "${URL}" -o "${TESTS_DIR}/toolchain/${ARCHIVE}"
tar -xzf "${TESTS_DIR}/toolchain/${ARCHIVE}" -C "${TESTS_DIR}/toolchain"
mv "${TESTS_DIR}/toolchain/lvgl-${LVGL_VERSION}" "${DEST}"
rm "${TESTS_DIR}/toolchain/${ARCHIVE}"

echo "${DEST}"
