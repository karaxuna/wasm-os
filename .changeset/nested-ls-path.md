---
"@wasm-os/sdk": minor
"@wasm-os/cli": minor
---

`listFiles(path?)` and `wasm-os ls [path]` now enumerate nested directories, not just the filesystem root. Needs firmware with LIST path support.
