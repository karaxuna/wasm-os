const { buildLittlefsImage, littlefsImageSegments, LFS_BLOCK_SIZE } = require("../src/mklfs");
const { parsePartitionTable, findPartition } = require("../src/partitions");

const textEncoder = new TextEncoder();

describe("littlefs image building", () => {
  it("formats a mountable-looking filesystem with the pushed files", async () => {
    const wasm = Uint8Array.from({ length: 5000 }, (_, i) => i & 0xff);
    const env = textEncoder.encode("WIFI_SSID=net\nWIFI_PASS=secret\n");
    const partitionSize = 0x80000; // the 4MB profile's littlefs partition

    const image = await buildLittlefsImage(
      [
        { name: "main.wasm", data: wasm },
        { name: ".env", data: env },
      ],
      partitionSize
    );

    expect(image.length).toBe(partitionSize);
    // The superblock in block 0 carries the ASCII magic "littlefs".
    const block0 = Buffer.from(image.subarray(0, LFS_BLOCK_SIZE));
    expect(block0.includes("littlefs")).toBe(true);
    // File contents land in the image verbatim (no compression/encryption).
    expect(Buffer.from(image).includes(Buffer.from(env))).toBe(true);
  });

  it("rejects files that cannot fit", async () => {
    const partitionSize = 8 * LFS_BLOCK_SIZE; // tiny fs: metadata leaves little room
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
      expect(seg.address % LFS_BLOCK_SIZE).toBe(base % LFS_BLOCK_SIZE);
      expect(seg.data.length % LFS_BLOCK_SIZE).toBe(0);
      // Reassembling segments over an erased partition reproduces the image.
      expect([...seg.data]).toEqual([...image.subarray(seg.address - base, seg.address - base + seg.data.length)]);
      written += seg.data.length;
    }
    // A tiny filesystem writes a few blocks, not the whole partition.
    expect(written).toBeLessThan(0x80000 / 4);
  });
});

describe("partition table parsing", () => {
  function entry(label, type, subtype, offset, size) {
    const bytes = new Uint8Array(32);
    bytes[0] = 0xaa;
    bytes[1] = 0x50;
    bytes[2] = type;
    bytes[3] = subtype;
    const view = new DataView(bytes.buffer);
    view.setUint32(4, offset, true);
    view.setUint32(8, size, true);
    bytes.set(textEncoder.encode(label), 12);
    return bytes;
  }

  function imageWithTable(...entries) {
    const image = new Uint8Array(0x8000 + entries.length * 32 + 32).fill(0xff);
    entries.forEach((e, i) => {
      image.set(e, 0x8000 + i * 32);
    });
    return image;
  }

  it("parses entries until the magic ends", () => {
    const image = imageWithTable(
      entry("nvs", 1, 2, 0x9000, 0x5000),
      entry("littlefs", 1, 0x83, 0x370000, 0x80000)
    );

    const parsed = parsePartitionTable(image);
    expect(parsed).toHaveLength(2);
    expect(parsed[1]).toEqual({ label: "littlefs", type: 1, subtype: 0x83, offset: 0x370000, size: 0x80000 });
  });

  it("finds the littlefs partition by label", () => {
    const image = imageWithTable(entry("littlefs", 1, 0x83, 0x810000, 0x7d0000));
    expect(findPartition(image, "littlefs")).toMatchObject({ offset: 0x810000, size: 0x7d0000 });
    expect(findPartition(image, "coredump")).toBeNull();
  });
});
