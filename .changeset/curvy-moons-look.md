---
"@wasm-os/sdk": minor
"@wasm-os/cli": minor
---

Add `listFiles()` to the device client and a `wasm-os ls` command that enumerate the device filesystem over the new LIST serial command. Needs firmware with LIST support (serial_cmd 0x06).
