const {
  MAGIC,
  CMD,
  RSP,
  FRAME_HEADER_SIZE,
  buildFrame,
  buildPushBeginFrame,
  buildPushDataFrame,
  buildPushEndFrame,
  buildRestartFrame,
  parseResponse,
} = require("../src/protocol");

describe("frame building", () => {
  it("lays out header and payload correctly", () => {
    const payload = Buffer.from([0xaa, 0xbb, 0xcc]);
    const frame = buildFrame(CMD.PUSH_DATA, payload);

    expect(frame.length).toBe(FRAME_HEADER_SIZE + 3);
    expect(frame.subarray(0, 4)).toEqual(MAGIC);
    expect(frame[4]).toBe(CMD.PUSH_DATA);
    expect(frame.readUInt32LE(5)).toBe(3);
    expect(frame.subarray(FRAME_HEADER_SIZE)).toEqual(payload);
  });

  it("builds an empty-payload frame", () => {
    const frame = buildPushEndFrame();
    expect(frame.length).toBe(FRAME_HEADER_SIZE);
    expect(frame[4]).toBe(CMD.PUSH_END);
    expect(frame.readUInt32LE(5)).toBe(0);
  });

  it("encodes PUSH_BEGIN as size (LE) followed by the filename", () => {
    const frame = buildPushBeginFrame(0x12345, "main.wasm");
    expect(frame[4]).toBe(CMD.PUSH_BEGIN);
    expect(frame.readUInt32LE(FRAME_HEADER_SIZE)).toBe(0x12345);
    expect(frame.subarray(FRAME_HEADER_SIZE + 4).toString("utf8")).toBe("main.wasm");
  });

  it("builds a RESTART frame", () => {
    expect(buildRestartFrame()[4]).toBe(CMD.RESTART);
  });
});

describe("response parsing", () => {
  it("round-trips a built frame", () => {
    const payload = Buffer.from("hello");
    const frame = buildFrame(RSP.ACK, payload);

    const result = parseResponse(frame);
    expect(result).not.toBeNull();
    expect(result.cmd).toBe(RSP.ACK);
    expect(result.payload).toEqual(payload);
    expect(result.consumed).toBe(frame.length);
  });

  it("skips leading garbage (interleaved device logs)", () => {
    const garbage = Buffer.from("I (1234) app_runtime: Loaded main.wasm\n");
    const frame = buildFrame(RSP.NAK, Buffer.from("Out of memory"));
    const result = parseResponse(Buffer.concat([garbage, frame]));

    expect(result).not.toBeNull();
    expect(result.cmd).toBe(RSP.NAK);
    expect(result.payload.toString("utf8")).toBe("Out of memory");
    expect(result.consumed).toBe(garbage.length + frame.length);
  });

  it("returns null until the frame is complete", () => {
    const frame = buildFrame(RSP.ACK, Buffer.from("data"));
    for (let cut = 1; cut < frame.length; cut++) {
      expect(parseResponse(frame.subarray(0, cut))).toBeNull();
    }
    expect(parseResponse(frame)).not.toBeNull();
  });

  it("returns null when there is no magic at all", () => {
    expect(parseResponse(Buffer.from("just some log output, no frames here"))).toBeNull();
  });

  it("reports consumed so trailing bytes can be preserved", () => {
    const frame = buildFrame(RSP.ACK, Buffer.alloc(0));
    const trailing = Buffer.from("W"); // start of a possible next frame
    const buffer = Buffer.concat([frame, trailing]);

    const result = parseResponse(buffer);
    expect(result.consumed).toBe(frame.length);
    expect(buffer.subarray(result.consumed)).toEqual(trailing);
  });

  it("parses the first of several frames", () => {
    const first = buildFrame(RSP.ACK, Buffer.from("1"));
    const second = buildFrame(RSP.ACK, Buffer.from("2"));
    const buffer = Buffer.concat([first, second]);

    const result = parseResponse(buffer);
    expect(result.payload.toString("utf8")).toBe("1");

    const next = parseResponse(buffer.subarray(result.consumed));
    expect(next.payload.toString("utf8")).toBe("2");
  });
});
