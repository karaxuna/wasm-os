#!/usr/bin/env bash
# Compile the touch-button fixture to app.wasm with wasi-sdk (fetched on
# first run). Plain C against the wasm-os bindings; the whole app is a few
# kilobytes, well within what a PSRAM-less ESP32 can hold.
set -euo pipefail

FIXTURE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS_DIR="${FIXTURE_DIR}/../../scripts"

WASI_SDK="$("${SCRIPTS_DIR}/fetch-wasi-sdk.sh")"
CLANG="${WASI_SDK}/bin/clang"
OUT="${FIXTURE_DIR}/app.wasm"

"${CLANG}" \
  --target=wasm32-wasi \
  -Oz \
  -ffunction-sections -fdata-sections \
  -Wl,--gc-sections -Wl,--strip-all \
  -Wl,-z,stack-size=4096 \
  "${FIXTURE_DIR}"/*.c \
  -o "${OUT}"

if command -v wasm-opt >/dev/null 2>&1; then
  wasm-opt -Oz --enable-bulk-memory --enable-sign-ext --enable-nontrapping-float-to-int "${OUT}" -o "${OUT}"
fi

echo "Built ${OUT} ($(stat -f%z "${OUT}" 2>/dev/null || stat -c%s "${OUT}") bytes)"
