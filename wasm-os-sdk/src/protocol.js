// Serial protocol shared with the firmware (wasm-os-core/main/serial_cmd.c) —
// keep the two in sync. Frame format: [MAGIC:4] [CMD:1] [LEN:4 LE] [PAYLOAD:LEN]
//
// Pure Uint8Array/DataView so the same code runs in Node and the browser.

const MAGIC = Uint8Array.from([0x57, 0x4f, 0x53, 0x21]); // "WOS!"

const CMD = {
  PUSH_BEGIN: 0x01,
  PUSH_DATA: 0x02,
  PUSH_END: 0x03,
  RESTART: 0x04,
  DELETE: 0x05,
};

const RSP = {
  ACK: 0x80,
  NAK: 0x81,
};

const FRAME_HEADER_SIZE = 9; // 4 magic + 1 cmd + 4 len
const CHUNK_SIZE = 1024;

const textEncoder = new TextEncoder();

function buildFrame(cmd, payload) {
  let payloadBytes;
  if (!payload) {
    payloadBytes = new Uint8Array(0);
  } else if (payload instanceof Uint8Array) {
    payloadBytes = payload;
  } else {
    payloadBytes = Uint8Array.from(payload);
  }

  const frame = new Uint8Array(FRAME_HEADER_SIZE + payloadBytes.length);
  frame.set(MAGIC, 0);
  frame[4] = cmd;
  new DataView(frame.buffer).setUint32(5, payloadBytes.length, true);
  frame.set(payloadBytes, FRAME_HEADER_SIZE);
  return frame;
}

// PUSH_BEGIN payload: [total_size:4 LE] [filename:utf8]
function buildPushBeginFrame(totalSize, filename) {
  const nameBytes = textEncoder.encode(filename);
  const payload = new Uint8Array(4 + nameBytes.length);
  new DataView(payload.buffer).setUint32(0, totalSize, true);
  payload.set(nameBytes, 4);
  return buildFrame(CMD.PUSH_BEGIN, payload);
}

function buildPushDataFrame(chunk) {
  return buildFrame(CMD.PUSH_DATA, chunk);
}

function buildPushEndFrame() {
  return buildFrame(CMD.PUSH_END);
}

function buildRestartFrame() {
  return buildFrame(CMD.RESTART);
}

// DELETE payload: [filename:utf8]
function buildDeleteFrame(filename) {
  return buildFrame(CMD.DELETE, textEncoder.encode(filename));
}

/* Uint8Array has no subsequence indexOf, so scan by hand. */
function findMagic(buffer) {
  for (let i = 0; i + MAGIC.length <= buffer.length; i++) {
    if (buffer[i] === MAGIC[0] && buffer[i + 1] === MAGIC[1] && buffer[i + 2] === MAGIC[2] && buffer[i + 3] === MAGIC[3]) {
      return i;
    }
  }
  return -1;
}

/**
 * Scan a byte buffer for the next complete frame (device log output may
 * surround it). Returns { cmd, payload, consumed } — `consumed` is the
 * offset just past the frame, for callers that keep the buffer — or null
 * when no complete frame is present yet.
 */
function parseResponse(buffer) {
  const magicIdx = findMagic(buffer);
  if (magicIdx === -1 || buffer.length - magicIdx < FRAME_HEADER_SIZE) {
    return null;
  }

  const view = new DataView(buffer.buffer, buffer.byteOffset, buffer.byteLength);
  const cmd = buffer[magicIdx + 4];
  const payloadLen = view.getUint32(magicIdx + 5, true);
  const frameEnd = magicIdx + FRAME_HEADER_SIZE + payloadLen;
  if (buffer.length < frameEnd) {
    return null;
  }

  return {
    cmd,
    payload: buffer.subarray(magicIdx + FRAME_HEADER_SIZE, frameEnd),
    consumed: frameEnd,
  };
}

module.exports = {
  MAGIC,
  CMD,
  RSP,
  FRAME_HEADER_SIZE,
  CHUNK_SIZE,
  buildFrame,
  buildPushBeginFrame,
  buildPushDataFrame,
  buildPushEndFrame,
  buildRestartFrame,
  buildDeleteFrame,
  parseResponse,
};
