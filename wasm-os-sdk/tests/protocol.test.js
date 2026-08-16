const {
  MAGIC,
  CMD,
  RSP,
  FRAME_HEADER_SIZE,
  buildFrame,
  buildPushBeginFrame,
  buildPushEndFrame,
  buildRestartFrame,
  buildDeleteFrame,
  buildListFrame,
  parseFileList,
  parseResponse,
} = require("../src/protocol");

const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder();

function readU32(bytes, offset) {
  return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(offset, true);
}

function concat(...parts) {
  const total = parts.reduce((n, p) => {
    return n + p.length;
  }, 0);

  const out = new Uint8Array(total);
  let offset = 0;
  for (const part of parts) {
    out.set(part, offset);
    offset += part.length;
  }
  return out;
}

describe("frame building", () => {
  it("lays out header and payload correctly", () => {
    const payload = Uint8Array.from([0xaa, 0xbb, 0xcc]);
    const frame = buildFrame(CMD.PUSH_DATA, payload);

    expect(frame.length).toBe(FRAME_HEADER_SIZE + 3);
    expect([...frame.subarray(0, 4)]).toEqual([...MAGIC]);
    expect(frame[4]).toBe(CMD.PUSH_DATA);
    expect(readU32(frame, 5)).toBe(3);
    expect([...frame.subarray(FRAME_HEADER_SIZE)]).toEqual([...payload]);
  });

  it("builds an empty-payload frame", () => {
    const frame = buildPushEndFrame();
    expect(frame.length).toBe(FRAME_HEADER_SIZE);
    expect(frame[4]).toBe(CMD.PUSH_END);
    expect(readU32(frame, 5)).toBe(0);
  });

  it("encodes PUSH_BEGIN as size (LE) followed by the filename", () => {
    const frame = buildPushBeginFrame(0x12345, "main.wasm");
    expect(frame[4]).toBe(CMD.PUSH_BEGIN);
    expect(readU32(frame, FRAME_HEADER_SIZE)).toBe(0x12345);
    expect(textDecoder.decode(frame.subarray(FRAME_HEADER_SIZE + 4))).toBe("main.wasm");
  });

  it("builds a RESTART frame", () => {
    expect(buildRestartFrame()[4]).toBe(CMD.RESTART);
  });

  it("builds a LIST frame with no payload for the root", () => {
    const frame = buildListFrame();
    expect(frame[4]).toBe(CMD.LIST);
    expect(frame.length).toBe(FRAME_HEADER_SIZE);
  });

  it("encodes a LIST path as the payload", () => {
    const frame = buildListFrame("apps/demo");
    expect(frame[4]).toBe(CMD.LIST);
    expect(textDecoder.decode(frame.subarray(FRAME_HEADER_SIZE))).toBe("apps/demo");
  });

  it("encodes DELETE as a bare filename", () => {
    const frame = buildDeleteFrame("probe.bin");
    expect(frame[4]).toBe(CMD.DELETE);
    expect(readU32(frame, 5)).toBe("probe.bin".length);
    expect(textDecoder.decode(frame.subarray(FRAME_HEADER_SIZE))).toBe("probe.bin");
  });
});

describe("response parsing", () => {
  it("round-trips a built frame", () => {
    const payload = textEncoder.encode("hello");
    const frame = buildFrame(RSP.ACK, payload);

    const result = parseResponse(frame);
    expect(result).not.toBeNull();
    expect(result.cmd).toBe(RSP.ACK);
    expect([...result.payload]).toEqual([...payload]);
    expect(result.consumed).toBe(frame.length);
  });

  it("skips leading garbage (interleaved device logs)", () => {
    const garbage = textEncoder.encode("I (1234) app_runtime: Loaded main.wasm\n");
    const frame = buildFrame(RSP.NAK, textEncoder.encode("Out of memory"));
    const result = parseResponse(concat(garbage, frame));

    expect(result).not.toBeNull();
    expect(result.cmd).toBe(RSP.NAK);
    expect(textDecoder.decode(result.payload)).toBe("Out of memory");
    expect(result.consumed).toBe(garbage.length + frame.length);
  });

  it("returns null until the frame is complete", () => {
    const frame = buildFrame(RSP.ACK, textEncoder.encode("data"));
    for (let cut = 1; cut < frame.length; cut++) {
      expect(parseResponse(frame.subarray(0, cut))).toBeNull();
    }
    expect(parseResponse(frame)).not.toBeNull();
  });

  it("returns null when there is no magic at all", () => {
    expect(parseResponse(textEncoder.encode("just some log output, no frames here"))).toBeNull();
  });

  it("reports consumed so trailing bytes can be preserved", () => {
    const frame = buildFrame(RSP.ACK, new Uint8Array(0));
    const trailing = textEncoder.encode("W"); // start of a possible next frame
    const buffer = concat(frame, trailing);

    const result = parseResponse(buffer);
    expect(result.consumed).toBe(frame.length);
    expect([...buffer.subarray(result.consumed)]).toEqual([...trailing]);
  });

  it("parses the first of several frames", () => {
    const first = buildFrame(RSP.ACK, textEncoder.encode("1"));
    const second = buildFrame(RSP.ACK, textEncoder.encode("2"));
    const buffer = concat(first, second);

    const result = parseResponse(buffer);
    expect(textDecoder.decode(result.payload)).toBe("1");

    const next = parseResponse(buffer.subarray(result.consumed));
    expect(textDecoder.decode(next.payload)).toBe("2");
  });

  it("parses a LIST payload of typed entries", () => {
    // [type][size:4 LE][name_len][name] x2: main.wasm (12127 bytes), a dir
    const name1 = textEncoder.encode("main.wasm");
    const name2 = textEncoder.encode("apps");
    const payload = new Uint8Array(6 + name1.length + 6 + name2.length);
    const view = new DataView(payload.buffer);
    let off = 0;
    payload[off] = 0;
    view.setUint32(off + 1, 12127, true);
    payload[off + 5] = name1.length;
    payload.set(name1, off + 6);
    off += 6 + name1.length;
    payload[off] = 1;
    view.setUint32(off + 1, 0, true);
    payload[off + 5] = name2.length;
    payload.set(name2, off + 6);

    expect(parseFileList(payload)).toEqual([
      { name: "main.wasm", size: 12127, type: "file" },
      { name: "apps", size: 0, type: "dir" },
    ]);
    expect(parseFileList(new Uint8Array(0))).toEqual([]);
    expect(() => parseFileList(payload.subarray(0, 4))).toThrow(/Truncated/);
  });

  it("parses frames offered as Node Buffers (a Uint8Array subclass)", () => {
    const frame = Buffer.from(buildFrame(RSP.ACK, textEncoder.encode("buf")));
    const result = parseResponse(frame);
    expect(result.cmd).toBe(RSP.ACK);
    expect(textDecoder.decode(result.payload)).toBe("buf");
  });
});
