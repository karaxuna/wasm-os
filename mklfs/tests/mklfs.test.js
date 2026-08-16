const { execFileSync } = require("child_process");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { buildLittlefsImage, openLittlefsImage, littlefsImageSegments, DEFAULT_GEOMETRY } = require("../src/index");

const textEncoder = new TextEncoder();
const BLOCK = DEFAULT_GEOMETRY.blockSize;

describe("littlefs image building", () => {
  it("formats a mountable-looking filesystem with the pushed files", async () => {
    const wasm = Uint8Array.from({ length: 5000 }, (_, i) => i & 0xff);
    const env = textEncoder.encode("WIFI_SSID=net\nWIFI_PASS=secret\n");
    const partitionSize = 0x80000;

    const image = await buildLittlefsImage(
      [
        { name: "main.wasm", data: wasm },
        { name: ".env", data: env },
      ],
      partitionSize
    );

    expect(image.length).toBe(partitionSize);
    // The superblock in block 0 carries the ASCII magic "littlefs".
    const block0 = Buffer.from(image.subarray(0, BLOCK));
    expect(block0.includes("littlefs")).toBe(true);
    // File contents land in the image verbatim (no compression/encryption).
    expect(Buffer.from(image).includes(Buffer.from(env))).toBe(true);
  });

  it("creates parent directories for nested paths", async () => {
    const image = await buildLittlefsImage(
      [{ name: "apps/demo/config.json", data: textEncoder.encode("{}") }],
      0x80000
    );
    expect(Buffer.from(image).includes(Buffer.from("config.json"))).toBe(true);
    expect(Buffer.from(image).includes(Buffer.from("demo"))).toBe(true);
  });

  it("accepts a custom geometry", async () => {
    const image = await buildLittlefsImage([{ name: "a", data: textEncoder.encode("hi") }], 64 * 8192, {
      blockSize: 8192,
      readSize: 256,
      progSize: 256,
      cacheSize: 1024,
      lookaheadSize: 256,
    });
    expect(image.length).toBe(64 * 8192);
    expect(Buffer.from(image.subarray(0, 8192)).includes("littlefs")).toBe(true);
  });

  it("rejects an inconsistent geometry", async () => {
    // cache does not divide block
    await expect(
      buildLittlefsImage([], 0x80000, { blockSize: 4096, cacheSize: 3000 })
    ).rejects.toThrow(/format failed/);
  });

  it("rejects files that cannot fit", async () => {
    const partitionSize = 8 * BLOCK; // tiny fs: metadata leaves little room
    const big = new Uint8Array(partitionSize);
    await expect(buildLittlefsImage([{ name: "big.bin", data: big }], partitionSize)).rejects.toThrow(/littlefs/);
  });

  it("round-trips: written images list and read back byte-identical", async () => {
    const payload = Uint8Array.from({ length: 10000 }, (_, i) => (i * 7) & 0xff);
    const image = await buildLittlefsImage(
      [
        { name: "main.wasm", data: payload },
        { name: "apps/extra.bin", data: textEncoder.encode("extra") },
      ],
      0x80000
    );

    const mounted = await openLittlefsImage(image);
    const listing = mounted.list();
    const paths = listing.map((e) => e.path).sort();
    expect(paths).toEqual(["apps", "apps/extra.bin", "main.wasm"]);
    expect(listing.find((e) => e.path === "main.wasm").size).toBe(payload.length);

    expect([...mounted.readFile("main.wasm")]).toEqual([...payload]);
    expect(new TextDecoder().decode(mounted.readFile("apps/extra.bin"))).toBe("extra");
  });

  it("refuses to mount with the wrong block size", async () => {
    // The superblock records the block size, so a mismatch is detectable;
    // read/prog sizes are host-side and silently accepted.
    const image = await buildLittlefsImage([{ name: "a", data: textEncoder.encode("x") }], 0x80000);
    await expect(
      openLittlefsImage(image, { blockSize: 8192, cacheSize: 1024, readSize: 256, progSize: 256 })
    ).rejects.toThrow(/mount failed/);
  });

  it("splits the image into non-erased block runs", async () => {
    const image = await buildLittlefsImage([{ name: "a.txt", data: textEncoder.encode("hi") }], 0x80000);
    const base = 0x810000;
    const segments = littlefsImageSegments(image, base);

    expect(segments.length).toBeGreaterThan(0);
    let written = 0;
    for (const seg of segments) {
      expect(seg.address % BLOCK).toBe(base % BLOCK);
      expect(seg.data.length % BLOCK).toBe(0);
      // Reassembling segments over an erased partition reproduces the image.
      expect([...seg.data]).toEqual([...image.subarray(seg.address - base, seg.address - base + seg.data.length)]);
      written += seg.data.length;
    }
    // A tiny filesystem writes a few blocks, not the whole partition.
    expect(written).toBeLessThan(0x80000 / 4);
  });
});

describe("cli", () => {
  const CLI = path.resolve(__dirname, "../src/cli.js");

  it("create/list/unpack round-trip, mklittlefs-style flags", () => {
    const work = fs.mkdtempSync(path.join(os.tmpdir(), "mklfs-"));
    const src = path.join(work, "src");
    const out = path.join(work, "out");
    const imagePath = path.join(work, "fs.bin");
    fs.mkdirSync(path.join(src, "config"), { recursive: true });
    fs.writeFileSync(path.join(src, "main.wasm"), Buffer.from([1, 2, 3, 4]));
    fs.writeFileSync(path.join(src, "config", "app.json"), '{"a":1}');
    fs.writeFileSync(path.join(src, ".hidden"), "skip me");

    execFileSync("node", [CLI, "-c", src, "-s", "0x80000", "-b", "4096", "-p", "128", imagePath]);
    expect(fs.statSync(imagePath).size).toBe(0x80000);

    const listing = execFileSync("node", [CLI, "-l", "-p", "128", imagePath]).toString();
    expect(listing).toContain("main.wasm");
    expect(listing).toContain("config/app.json");
    expect(listing).not.toContain(".hidden"); // dotfiles skipped without -a

    execFileSync("node", [CLI, "-u", out, "-p", "128", imagePath]);
    expect([...fs.readFileSync(path.join(out, "main.wasm"))]).toEqual([1, 2, 3, 4]);
    expect(fs.readFileSync(path.join(out, "config", "app.json"), "utf8")).toBe('{"a":1}');

    fs.rmSync(work, { recursive: true, force: true });
  });

  it("defaults to mklittlefs geometry (page 256)", () => {
    const work = fs.mkdtempSync(path.join(os.tmpdir(), "mklfs-"));
    const src = path.join(work, "src");
    const imagePath = path.join(work, "fs.bin");
    fs.mkdirSync(src, { recursive: true });
    fs.writeFileSync(path.join(src, "a.txt"), "hello");

    execFileSync("node", [CLI, "-c", src, "-s", "0x40000", imagePath]);
    const listing = execFileSync("node", [CLI, "-l", imagePath]).toString();
    expect(listing).toContain("a.txt");

    fs.rmSync(work, { recursive: true, force: true });
  });
});
