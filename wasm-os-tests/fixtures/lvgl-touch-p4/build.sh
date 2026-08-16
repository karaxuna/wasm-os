#!/usr/bin/env bash
# Compile the lvgl-touch-p4 fixture to app.wasm with wasi-sdk and LVGL (both
# fetched on first run). Object files are cached under build/ so repeat runs
# only recompile what changed; LVGL sources compile once.
set -euo pipefail

FIXTURE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS_DIR="${FIXTURE_DIR}/../../scripts"

WASI_SDK="$("${SCRIPTS_DIR}/fetch-wasi-sdk.sh")"
LVGL_DIR="$("${SCRIPTS_DIR}/fetch-lvgl.sh")"
CLANG="${WASI_SDK}/bin/clang"
OUT="${FIXTURE_DIR}/app.wasm"
BUILD_DIR="${FIXTURE_DIR}/build"

# -O3 renders ~noticeably faster under the WAMR interpreter than -Oz;
# override with WASM_OS_OPT=-Oz when size matters more than speed.
OPT="${WASM_OS_OPT:--O3}"

CFLAGS=(
  --target=wasm32-wasi
  "${OPT}"
  -ffunction-sections -fdata-sections
  -DLV_CONF_INCLUDE_SIMPLE
  -I"${FIXTURE_DIR}"
  -I"${LVGL_DIR}"
)

mkdir -p "${BUILD_DIR}"

OBJECTS=()
compile() {
  local src="$1" obj="$2"
  OBJECTS+=("${obj}")
  if [ "${obj}" -nt "${src}" ] 2>/dev/null; then
    return
  fi
  "${CLANG}" "${CFLAGS[@]}" -c "${src}" -o "${obj}"
}

# Fixture sources are always recompiled: the mtime check cannot see header
# changes (jd9365_init_cmds.h, gsl_fw.h, ...) and they compile in a blink.
for src in "${FIXTURE_DIR}"/*.c; do
  rm -f "${BUILD_DIR}/$(basename "${src%.c}").o"
  compile "${src}" "${BUILD_DIR}/$(basename "${src%.c}").o"
done

echo "Compiling LVGL (cached after the first run)..."
while IFS= read -r src; do
  obj="${BUILD_DIR}/lvgl-$(echo "${src#"${LVGL_DIR}"/src/}" | tr / _ | sed 's/\.c$/.o/')"
  compile "${src}" "${obj}"
done < <(find "${LVGL_DIR}/src" -name '*.c' | sort)

"${CLANG}" \
  --target=wasm32-wasi \
  "${OPT}" \
  -Wl,--gc-sections -Wl,--strip-all \
  -Wl,-z,stack-size=65536 \
  "${OBJECTS[@]}" \
  -o "${OUT}"

if command -v wasm-opt >/dev/null 2>&1; then
  wasm-opt "${OPT}" --enable-bulk-memory --enable-sign-ext --enable-nontrapping-float-to-int "${OUT}" -o "${OUT}"
fi

echo "Built ${OUT} ($(stat -f%z "${OUT}" 2>/dev/null || stat -c%s "${OUT}") bytes)"
