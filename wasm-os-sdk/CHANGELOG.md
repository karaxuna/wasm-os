# @wasm-os/sdk

## 0.3.0

### Minor Changes

- 4d2d970: `listFiles(path?)` and `wasm-os ls [path]` now enumerate nested directories, not just the filesystem root. Needs firmware with LIST path support.

## 0.2.0

### Minor Changes

- 7b6e6fb: Add `listFiles()` to the device client and a `wasm-os ls` command that enumerate the device filesystem over the new LIST serial command. Needs firmware with LIST support (serial_cmd 0x06).
