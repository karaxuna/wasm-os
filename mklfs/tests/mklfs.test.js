const { buildLittlefsImage, littlefsImageSegments, DEFAULT_GEOMETRY } = require("../src/index");

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
