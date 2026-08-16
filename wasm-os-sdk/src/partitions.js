// ESP-IDF partition table parsing: 32-byte entries at flash offset 0x8000,
// magic 0xAA 0x50, little-endian offset/size, NUL-padded 16-byte label.
// Reading it out of a merged image beats hardcoding per-profile offsets.

const PARTITION_TABLE_OFFSET = 0x8000;
const ENTRY_SIZE = 32;

const textDecoder = new TextDecoder();

/** Parse the partition table inside a merged firmware image. */
function parsePartitionTable(imageBytes, tableOffset = PARTITION_TABLE_OFFSET) {
  const entries = [];

  for (let off = tableOffset; off + ENTRY_SIZE <= imageBytes.length; off += ENTRY_SIZE) {
    if (imageBytes[off] !== 0xaa || imageBytes[off + 1] !== 0x50) {
      break;
    }

    const view = new DataView(imageBytes.buffer, imageBytes.byteOffset + off, ENTRY_SIZE);
    const labelBytes = imageBytes.subarray(off + 12, off + 28);
    let labelEnd = labelBytes.indexOf(0);
    if (labelEnd === -1) {
      labelEnd = labelBytes.length;
    }

    entries.push({
      type: imageBytes[off + 2],
      subtype: imageBytes[off + 3],
      offset: view.getUint32(4, true),
      size: view.getUint32(8, true),
      label: textDecoder.decode(labelBytes.subarray(0, labelEnd)),
    });
  }

  return entries;
}

/** The named partition entry, or null. */
function findPartition(imageBytes, label, tableOffset = PARTITION_TABLE_OFFSET) {
  const match = parsePartitionTable(imageBytes, tableOffset).find((p) => {
    return p.label === label;
  });
  return match || null;
}

module.exports = {
  PARTITION_TABLE_OFFSET,
  parsePartitionTable,
  findPartition,
};
