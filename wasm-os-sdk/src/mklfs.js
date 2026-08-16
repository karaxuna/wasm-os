// littlefs image building on the bundled mklfs.wasm (littlefs compiled to
// WebAssembly, no imports — see ../mklfs/). Works in Node and the browser;
// browsers must pass the module bytes in, Node loads the asset itself.

const LFS_BLOCK_SIZE = 4096;

const textEncoder = new TextEncoder();

async function instantiate(wasmBytes) {
  if (!wasmBytes) {
    const fs = require("fs");
    const path = require("path");
    wasmBytes = fs.readFileSync(path.join(__dirname, "../assets/mklfs.wasm"));
  }

  const { instance } = await WebAssembly.instantiate(wasmBytes, {});
  if (instance.exports._initialize) {
    instance.exports._initialize();
  }
  return instance;
}

/**
 * Build a littlefs image containing `files` ([{name, data}], flat namespace)
 * for a partition of `partitionSize` bytes. Geometry matches the firmware's
 * esp_littlefs. Returns the full-partition image as a Uint8Array.
 */
async function buildLittlefsImage(files, partitionSize, { wasmBytes } = {}) {
  const instance = await instantiate(wasmBytes);
  const exports = instance.exports;
  const mem = () => {
    return new Uint8Array(exports.memory.buffer);
  };

  const blockCount = Math.floor(partitionSize / LFS_BLOCK_SIZE);
  let rc = exports.mk_create(blockCount);
  if (rc !== 0) {
    throw new Error(`littlefs format failed: ${rc}`);
  }

  for (const { name, data } of files) {
    const nameBytes = textEncoder.encode(name);
    if (nameBytes.length + 1 > exports.mk_name_cap()) {
      throw new Error(`Filename too long: ${name}`);
    }
    if (data.length > exports.mk_file_cap()) {
      throw new Error(`${name} is ${data.length} bytes; mklfs caps files at ${exports.mk_file_cap()}`);
    }

    const memory = mem();
    memory.set(nameBytes, exports.mk_name_buf());
    memory[exports.mk_name_buf() + nameBytes.length] = 0;
    memory.set(data, exports.mk_file_buf());

    rc = exports.mk_add_file(data.length);
    if (rc !== 0) {
      throw new Error(`littlefs write failed for ${name}: ${rc}`);
    }
  }

  rc = exports.mk_finish();
  if (rc !== 0) {
    throw new Error(`littlefs unmount failed: ${rc}`);
  }

  const imagePtr = exports.mk_image_ptr();
  return mem().slice(imagePtr, imagePtr + blockCount * LFS_BLOCK_SIZE);
}

/**
 * Split the image into its written parts: erased flash already reads 0xFF,
 * and littlefs erases blocks before first use, so flashing only the non-FF
 * block runs yields an identical filesystem while writing a fraction of the
 * partition (littlefs seeds file placement mid-partition, so a simple prefix
 * trim would still cover megabytes of erased blocks).
 *
 * Returns [{ address, data }] with addresses relative to `baseAddress`.
 */
function littlefsImageSegments(image, baseAddress = 0) {
  const segments = [];
  let runStart = -1;

  for (let off = 0; off <= image.length; off += LFS_BLOCK_SIZE) {
    const blank = off >= image.length || image.subarray(off, off + LFS_BLOCK_SIZE).every((b) => b === 0xff);
    if (!blank && runStart === -1) {
      runStart = off;
    } else if (blank && runStart !== -1) {
      segments.push({ address: baseAddress + runStart, data: image.subarray(runStart, off) });
      runStart = -1;
    }
  }

  return segments;
}

module.exports = {
  LFS_BLOCK_SIZE,
  buildLittlefsImage,
  littlefsImageSegments,
};
