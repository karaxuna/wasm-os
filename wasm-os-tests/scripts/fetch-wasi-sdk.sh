#!/usr/bin/env bash
# Download a pinned wasi-sdk release into wasm-os-tests/toolchain/wasi-sdk (gitignored).
# Skipped when WASI_SDK_PATH is set or the toolchain is already present.
# Prints the toolchain path on success.
set -euo pipefail

WASI_SDK_VERSION_MAJOR=25
WASI_SDK_VERSION="${WASI_SDK_VERSION_MAJOR}.0"

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${TESTS_DIR}/toolchain/wasi-sdk"

if [ -n "${WASI_SDK_PATH:-}" ]; then
  echo "${WASI_SDK_PATH}"
  exit 0
fi

if [ -x "${DEST}/bin/clang" ]; then
  echo "${DEST}"
  exit 0
fi

case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) ARCHIVE_ARCH="arm64-macos" ;;
  Darwin-x86_64) ARCHIVE_ARCH="x86_64-macos" ;;
  Linux-x86_64) ARCHIVE_ARCH="x86_64-linux" ;;
  Linux-aarch64) ARCHIVE_ARCH="arm64-linux" ;;
  *)
    echo "Unsupported platform $(uname -s)-$(uname -m); set WASI_SDK_PATH manually" >&2
    exit 1
    ;;
esac

ARCHIVE="wasi-sdk-${WASI_SDK_VERSION}-${ARCHIVE_ARCH}.tar.gz"
URL="https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-${WASI_SDK_VERSION_MAJOR}/${ARCHIVE}"

echo "Downloading wasi-sdk ${WASI_SDK_VERSION} (${ARCHIVE_ARCH})..." >&2
mkdir -p "${TESTS_DIR}/toolchain"
curl -fLsS --retry 3 --retry-delay 2 -C - "${URL}" -o "${TESTS_DIR}/toolchain/${ARCHIVE}"
tar -xzf "${TESTS_DIR}/toolchain/${ARCHIVE}" -C "${TESTS_DIR}/toolchain"
mv "${TESTS_DIR}/toolchain/wasi-sdk-${WASI_SDK_VERSION}-${ARCHIVE_ARCH}" "${DEST}"
rm "${TESTS_DIR}/toolchain/${ARCHIVE}"

echo "${DEST}"
