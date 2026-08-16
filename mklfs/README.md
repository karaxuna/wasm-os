# @wasm-os/mklfs

Build and read [littlefs](https://github.com/littlefs-project/littlefs)
filesystem images in pure JavaScript. littlefs itself is compiled to a
~24 KB WebAssembly module with **zero imports**, so the same package runs in
Node and in the browser — no `mklittlefs` binary to download per platform,
no toolchain, no native dependencies.

Made for flashing pre-provisioned filesystems onto microcontrollers (ESP32
and friends) — from a build script, a CI job, or straight from a web page
over Web Serial.

- **Create** images from a directory (CLI) or a file list (API)
- **List** and **unpack** existing images
- **Drop-in `mklittlefs` replacement**: same `-c` / `-l` / `-u` / `-b` /
  `-p` / `-s` flags, same defaults
- Bundled littlefs **v2.11** (disk format 2.1)

## Install

```bash
npm install @wasm-os/mklfs        # library
npm install -g @wasm-os/mklfs     # CLI (installs the `mklfs` command)
```

## CLI

Flag-compatible with the C [mklittlefs](https://github.com/earlephilhower/mklittlefs),
so it can replace it in existing build scripts:

```bash
# Pack a directory into a 512 KB image
mklfs -c ./data -s 0x80000 littlefs.bin

# List an image's contents
mklfs -l littlefs.bin

# Unpack an image into a directory
mklfs -u ./extracted littlefs.bin
```

| Flag | Meaning | Default |
|---|---|---|
| `-c <dir>` | create: pack this directory | — |
| `-l` | list files in the image | — |
| `-u <dir>` | unpack the image into this directory | — |
| `-s <size>` | image size in bytes, `0x...` accepted (create only) | required |
| `-b <block>` | block size | `4096` |
| `-p <page>` | read/prog size | `256` (mklittlefs-compatible) |
| `-a` | include dotfiles | off |
| `-d <level>` | accepted for mklittlefs compatibility | ignored |

> **ESP-IDF users:** `esp_littlefs` mounts with page size **128**, not 256 —
> pass `-p 128`. Images only mount when the geometry matches the mounting
> side.

## Library

### Create

```js
const { buildLittlefsImage } = require("@wasm-os/mklfs");

const image = await buildLittlefsImage(
  [
    { name: "main.wasm", data: appBytes },
    { name: "config/settings.json", data: settingsBytes }, // dirs auto-created
  ],
  0x80000 // partition size in bytes
);
// image: Uint8Array covering the whole partition; unwritten blocks read
// 0xFF (erased flash). Flash it at the partition's offset.
```

### Read

```js
const { openLittlefsImage } = require("@wasm-os/mklfs");

const mounted = await openLittlefsImage(imageBytes);
mounted.list();               // [{ path, type: "file" | "dir", size }]
mounted.readFile("main.wasm"); // Uint8Array
```

### Browser

Everything works in the browser; pass the WASM module bytes in, since there
is no filesystem to load the asset from:

```js
import { buildLittlefsImage } from "@wasm-os/mklfs";

const wasmBytes = await fetch(mklfsWasmUrl).then((r) => r.arrayBuffer());
const image = await buildLittlefsImage(files, size, { wasmBytes });
```

(`mklfsWasmUrl` should serve `node_modules/@wasm-os/mklfs/assets/mklfs.wasm`; most
bundlers handle this with `new URL("@wasm-os/mklfs/assets/mklfs.wasm", import.meta.url)`.)

### Geometry

An image only mounts if its geometry matches the mounting side. The
**library** defaults match ESP-IDF's `esp_littlefs`; the **CLI** defaults
match the C `mklittlefs`:

```js
await buildLittlefsImage(files, size, {
  blockSize: 4096,    // must equal the mounting side's erase block
  readSize: 128,      // CLI default: 256
  progSize: 128,      // CLI default: 256
  cacheSize: 512,     // read/prog must divide it; it must divide blockSize
  lookaheadSize: 128, // multiple of 8
});
```

Common targets:

| Target | block | read/prog |
|---|---|---|
| ESP-IDF `esp_littlefs` (defaults) | 4096 | 128 |
| Arduino ESP8266/ESP32 LittleFS | 4096 | 256 |

### Sparse flashing

```js
const { littlefsImageSegments } = require("@wasm-os/mklfs");
const segments = littlefsImageSegments(image, partitionOffset);
// [{ address, data }] — only the non-0xFF block runs
```

Only safe when the target partition is **erased first**: littlefs metadata
pairs carry revision counts, and a stale block from a previous filesystem
can out-revision a freshly written one. When in doubt, flash the full image —
the 0xFF bulk compresses to almost nothing over serial.

## Limits

- Max image 16 MB, max single file 8 MB, block cycles fixed at 512.
- Disk format is littlefs 2.1 (lfs v2.11); the mounting side must speak it.
- One image at a time per module instance (the API hides this).

## Rebuilding the WASM module

`./build.sh` fetches the pinned littlefs source plus wasi-sdk and recompiles
`assets/mklfs.wasm`. Only needed when bumping littlefs or changing
`mklfs.c`. The module stays import-free by design — validate with
`WebAssembly.Module.imports()` after rebuilding.

## Tests

```bash
npm test
```

## License

MIT. littlefs is BSD-3-Clause, © Arm Limited.
