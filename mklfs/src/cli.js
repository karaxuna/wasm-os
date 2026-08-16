#!/usr/bin/env node

// Flag-compatible with the C mklittlefs tool, so it can drop into existing
// build scripts: -c/-l/-u with -b/-p/-s. CLI defaults mirror mklittlefs
// (block 4096, page 256); the library API's defaults mirror esp_littlefs
// (page 128) — geometry must match whatever mounts the image either way.

const fs = require("fs");
const path = require("path");
const { buildLittlefsImage, openLittlefsImage } = require("./index");

const USAGE = `Usage:
  mklfs -c <dir> -s <size> [-b <block>] [-p <page>] [-a] <image>   create image from directory
  mklfs -l [-b <block>] [-p <page>] <image>                        list image contents
  mklfs -u <dir> [-b <block>] [-p <page>] <image>                  unpack image into directory

Options:
  -c <dir>    create: pack this directory into the image
  -u <dir>    unpack: extract the image into this directory
  -l          list files in the image
  -s <size>   image size in bytes (create only; hex like 0x80000 works)
  -b <block>  block size (default 4096; must match the mounting side)
  -p <page>   read/prog size (default 256, like mklittlefs; esp_littlefs uses 128)
  -a          include files whose names start with a dot
  -d <level>  accepted for mklittlefs compatibility; ignored
  -h, --help  show this help
  --version   print the package version`;

function fail(message) {
  console.error(message);
  process.exit(1);
}

function parseSize(text) {
  const value = parseInt(text, text.startsWith("0x") ? 16 : 10);
  if (Number.isNaN(value) || value <= 0) {
    fail(`Invalid size: ${text}`);
  }
  return value;
}

function parseArgs(argv) {
  const opts = { block: 4096, page: 256, allFiles: false };
  const positional = [];

  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    const next = () => {
      i++;
      if (i >= argv.length) {
        fail(`Missing value for ${arg}`);
      }
      return argv[i];
    };

    if (arg === "-c") {
      opts.mode = "create";
      opts.dir = next();
    } else if (arg === "-u") {
      opts.mode = "unpack";
      opts.dir = next();
    } else if (arg === "-l") {
      opts.mode = "list";
    } else if (arg === "-s") {
      opts.size = parseSize(next());
    } else if (arg === "-b") {
      opts.block = parseSize(next());
    } else if (arg === "-p") {
      opts.page = parseSize(next());
    } else if (arg === "-a" || arg === "--all-files") {
      opts.allFiles = true;
    } else if (arg === "-d") {
      next(); // mklittlefs debug level; ignored
    } else if (arg === "-h" || arg === "--help") {
      console.log(USAGE);
      process.exit(0);
    } else if (arg === "--version") {
      console.log(require("../package.json").version);
      process.exit(0);
    } else if (arg.startsWith("-")) {
      fail(`Unknown option: ${arg}\n\n${USAGE}`);
    } else {
      positional.push(arg);
    }
  }

  if (!opts.mode) {
    fail(USAGE);
  }
  if (positional.length !== 1) {
    fail(`Expected exactly one image path\n\n${USAGE}`);
  }
  opts.image = positional[0];
  return opts;
}

/* Pick a cache size littlefs accepts: a multiple of the page that divides
 * the block, at least 512 when possible. */
function geometryFor(opts) {
  let cacheSize = opts.page;
  while (cacheSize < 512 && opts.block % (cacheSize * 2) === 0) {
    cacheSize *= 2;
  }
  return {
    blockSize: opts.block,
    readSize: opts.page,
    progSize: opts.page,
    cacheSize,
    lookaheadSize: 128,
  };
}

function collectFiles(rootDir, allFiles) {
  const files = [];
  const walk = (dir, prefix) => {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
      if (!allFiles && entry.name.startsWith(".")) {
        continue;
      }
      const full = path.join(dir, entry.name);
      const name = prefix === "" ? entry.name : `${prefix}/${entry.name}`;
      if (entry.isDirectory()) {
        walk(full, name);
      } else if (entry.isFile()) {
        files.push({ name, data: fs.readFileSync(full) });
      }
    }
  };
  walk(rootDir, "");
  return files;
}

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  const geometry = geometryFor(opts);

  if (opts.mode === "create") {
    if (!opts.size) {
      fail("Create mode needs -s <size>");
    }
    if (!fs.existsSync(opts.dir) || !fs.statSync(opts.dir).isDirectory()) {
      fail(`Not a directory: ${opts.dir}`);
    }

    const files = collectFiles(opts.dir, opts.allFiles);
    const image = await buildLittlefsImage(files, opts.size, geometry);
    fs.writeFileSync(opts.image, image);
    for (const file of files) {
      console.log(`${file.data.length}\t${file.name}`);
    }
    console.log(`Wrote ${image.length} bytes (${files.length} files) to ${opts.image}`);
    return;
  }

  const image = new Uint8Array(fs.readFileSync(opts.image));
  const mounted = await openLittlefsImage(image, geometry);

  if (opts.mode === "list") {
    for (const entry of mounted.list()) {
      console.log(entry.type === "dir" ? `<dir>\t${entry.path}` : `${entry.size}\t${entry.path}`);
    }
    return;
  }

  // unpack
  for (const entry of mounted.list()) {
    const dest = path.join(opts.dir, entry.path);
    if (entry.type === "dir") {
      fs.mkdirSync(dest, { recursive: true });
    } else {
      fs.mkdirSync(path.dirname(dest), { recursive: true });
      fs.writeFileSync(dest, mounted.readFile(entry.path));
      console.log(`${entry.size}\t${entry.path}`);
    }
  }
}

main().catch((err) => {
  fail(`Error: ${err.message}`);
});
