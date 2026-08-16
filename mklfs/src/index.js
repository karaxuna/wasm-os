// littlefs image building on the bundled mklfs.wasm (littlefs compiled to
// WebAssembly, no imports — see ../mklfs.c). Works in Node and the browser;
// browsers must pass the module bytes in, Node loads the asset itself.

/**
 * Default geometry, matching ESP-IDF's esp_littlefs defaults. An image only
 * mounts if these match the mounting side's configuration.
 */
const DEFAULT_GEOMETRY = {
  blockSize: 4096,
  readSize: 128,
  progSize: 128,
  cacheSize: 512,
  lookaheadSize: 128,
};

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

function writeName(exports, mem, name) {
  const nameBytes = textEncoder.encode(name);
  if (nameBytes.length + 1 > exports.mk_name_cap()) {
    throw new Error(`Path too long: ${name}`);
  }
  mem.set(nameBytes, exports.mk_name_buf());
  mem[exports.mk_name_buf() + nameBytes.length] = 0;
}

/**
 * Build a littlefs image containing `files` ([{ name, data }]; names may
 * contain '/' — parent directories are created automatically) for a
 * partition of `partitionSize` bytes. Returns the full-partition image as a
 * Uint8Array; unwritten blocks read 0xFF, i.e. erased flash.
 */
async function buildLittlefsImage(files, partitionSize, options = {}) {
  const { wasmBytes, ...geometryOverrides } = options;
  const geometry = { ...DEFAULT_GEOMETRY, ...geometryOverrides };

  const instance = await instantiate(wasmBytes);
  const exports = instance.exports;
  const mem = () => {
    return new Uint8Array(exports.memory.buffer);
  };

  const blockCount = Math.floor(partitionSize / geometry.blockSize);
  let rc = exports.mk_create(
    geometry.blockSize,
    blockCount,
    geometry.readSize,
    geometry.progSize,
    geometry.cacheSize,
    geometry.lookaheadSize
  );
  if (rc !== 0) {
    throw new Error(`littlefs format failed: ${rc}`);
  }

  const knownDirs = new Set();
  for (const { name, data } of files) {
    // Create parent directories, outermost first.
    const parts = name.split("/").filter((p) => {
      return p.length > 0;
    });
    for (let i = 1; i < parts.length; i++) {
      const dir = parts.slice(0, i).join("/");
      if (knownDirs.has(dir)) {
        continue;
      }
      writeName(exports, mem(), dir);
      rc = exports.mk_mkdir();
      if (rc !== 0 && rc !== -17 /* LFS_ERR_EXIST */) {
        throw new Error(`littlefs mkdir failed for ${dir}: ${rc}`);
      }
      knownDirs.add(dir);
    }

    if (data.length > exports.mk_file_cap()) {
      throw new Error(`${name} is ${data.length} bytes; mklfs caps files at ${exports.mk_file_cap()}`);
    }
    writeName(exports, mem(), name);
    mem().set(data, exports.mk_file_buf());

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
  return mem().slice(imagePtr, imagePtr + blockCount * geometry.blockSize);
}

/**
 * Mount an existing littlefs image for reading. The image length must be a
 * multiple of the block size; geometry must match whatever built the image.
 * Returns { list, readFile } — `list()` walks the whole tree and returns
 * [{ path, type: "file"|"dir", size }], `readFile(path)` returns the bytes.
 */
async function openLittlefsImage(image, options = {}) {
  const { wasmBytes, ...geometryOverrides } = options;
  const geometry = { ...DEFAULT_GEOMETRY, ...geometryOverrides };
  if (image.length === 0 || image.length % geometry.blockSize !== 0) {
    throw new Error(`Image length ${image.length} is not a multiple of block size ${geometry.blockSize}`);
  }

  const instance = await instantiate(wasmBytes);
  const exports = instance.exports;
  const mem = () => {
    return new Uint8Array(exports.memory.buffer);
  };

  mem().set(image, exports.mk_image_ptr());
  const rc = exports.mk_mount(
    geometry.blockSize,
    image.length / geometry.blockSize,
    geometry.readSize,
    geometry.progSize,
    geometry.cacheSize,
    geometry.lookaheadSize
  );
  if (rc !== 0) {
    throw new Error(`littlefs mount failed: ${rc} (wrong geometry, or not a littlefs image)`);
  }

  const textDecoder = new TextDecoder();

  function readName(memory) {
    const base = exports.mk_name_buf();
    let end = base;
    while (memory[end] !== 0) {
      end++;
    }
    return textDecoder.decode(memory.subarray(base, end));
  }

  // A single directory handle exists on the wasm side, so the walk reads
  // each directory completely before descending.
  function listDir(dirPath) {
    writeName(exports, mem(), dirPath === "" ? "/" : dirPath);
    let rc = exports.mk_dir_open();
    if (rc !== 0) {
      throw new Error(`littlefs opendir failed for ${dirPath || "/"}: ${rc}`);
    }

    const entries = [];
    while ((rc = exports.mk_dir_next()) > 0) {
      const name = readName(mem());
      entries.push({
        path: dirPath === "" ? name : `${dirPath}/${name}`,
        type: rc === 2 ? "dir" : "file",
        size: rc === 2 ? 0 : exports.mk_entry_size(),
      });
    }
    exports.mk_dir_close();
    if (rc < 0) {
      throw new Error(`littlefs readdir failed in ${dirPath || "/"}: ${rc}`);
    }
    return entries;
  }

  function list() {
    const all = [];
    const queue = [""];
    while (queue.length > 0) {
      const dir = queue.shift();
      for (const entry of listDir(dir)) {
        all.push(entry);
        if (entry.type === "dir") {
          queue.push(entry.path);
        }
      }
    }
    return all;
  }

  function readFile(path) {
    writeName(exports, mem(), path);
    const size = exports.mk_read_file();
    if (size < 0) {
      throw new Error(`littlefs read failed for ${path}: ${size}`);
    }
    return mem().slice(exports.mk_file_buf(), exports.mk_file_buf() + size);
  }

  return { list, readFile };
}

/**
 * Split an image into its written parts: erased flash already reads 0xFF,
 * and littlefs erases blocks before first use, so flashing only the non-FF
 * block runs onto an ERASED partition yields an identical filesystem. Do not
 * use over a partition with previous contents — a stale littlefs metadata
 * block can out-revision a freshly written one; erase first, or flash the
 * full image.
 *
 * Returns [{ address, data }] with addresses relative to `baseAddress`.
 */
function littlefsImageSegments(image, baseAddress = 0, blockSize = DEFAULT_GEOMETRY.blockSize) {
  const segments = [];
  let runStart = -1;

  for (let off = 0; off <= image.length; off += blockSize) {
    const blank = off >= image.length || image.subarray(off, off + blockSize).every((b) => b === 0xff);
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
  DEFAULT_GEOMETRY,
  buildLittlefsImage,
  openLittlefsImage,
  littlefsImageSegments,
};
