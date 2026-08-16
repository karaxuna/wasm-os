# mklfs

Build [littlefs](https://github.com/littlefs-project/littlefs) filesystem
images in pure JavaScript. littlefs itself is compiled to a ~23 KB
WebAssembly module with **zero imports**, so the same package runs in Node
and in the browser — no `mklittlefs` binary, no toolchain, no native deps.

Made for flashing pre-provisioned filesystems onto microcontrollers (ESP32
and friends), from a CLI or straight from a web page over Web Serial.

## Usage

```js
const { buildLittlefsImage } = require("mklfs");

const image = await buildLittlefsImage(
  [
    { name: "main.wasm", data: appBytes },
    { name: "config/settings.json", data: settingsBytes }, // dirs auto-created
  ],
  0x80000 // partition size in bytes
);
// `image` is a Uint8Array covering the whole partition; unwritten blocks
// read 0xFF (erased flash). Flash it at the partition's offset.
```

In the browser, pass the module bytes in (there is no filesystem to load the
asset from):

```js
const wasmBytes = await fetch(mklfsWasmUrl).then((r) => r.arrayBuffer());
const image = await buildLittlefsImage(files, size, { wasmBytes });
```

## Geometry

An image only mounts if its geometry matches the mounting side. The defaults
match ESP-IDF's `esp_littlefs` defaults; every value can be overridden:

```js
await buildLittlefsImage(files, size, {
  blockSize: 4096,    // must equal the flash erase block the mounter uses
  readSize: 128,
  progSize: 128,
  cacheSize: 512,     // read/prog must divide it; it must divide blockSize
  lookaheadSize: 128, // multiple of 8
});
```

The bundled littlefs is **v2.11** (disk format 2.1). The mounting side must
speak the same disk version.

## Sparse flashing

```js
const { littlefsImageSegments } = require("mklfs");
const segments = littlefsImageSegments(image, partitionOffset);
// [{ address, data }] — only the non-0xFF block runs
```

Only safe when the target partition is erased first: littlefs metadata pairs
carry revision counts, and a stale block from a previous filesystem can
out-revision a freshly written one. When in doubt, flash the full image —
the 0xFF bulk compresses to almost nothing over serial.

## Limits

- Write-only: builds images, does not read them (yet).
- Max image 16 MB, max single file 8 MB, block cycles fixed at 512.

## Rebuilding the WASM module

`./build.sh` fetches the pinned littlefs source and wasi-sdk and recompiles
`assets/mklfs.wasm`. Only needed when bumping littlefs or changing `mklfs.c`.

## Tests

```bash
npm test
```
