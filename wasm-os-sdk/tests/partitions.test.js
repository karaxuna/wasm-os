const { parsePartitionTable, findPartition } = require("../src/partitions");

const textEncoder = new TextEncoder();

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
