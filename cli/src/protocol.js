// Serial protocol shared with the firmware (main/serial_cmd.h).
// Frame format: [MAGIC:4] [CMD:1] [LEN:4 LE] [PAYLOAD:LEN]

const MAGIC = Buffer.from([0x57, 0x4f, 0x53, 0x21]); // "WOS!"

const CMD = {
  PUSH_BEGIN: 0x01,
  PUSH_DATA: 0x02,
  PUSH_END: 0x03,
  RESTART: 0x04,
};

const RSP = {
  ACK: 0x80,
  NAK: 0x81,
};

const FRAME_HEADER_SIZE = 9; // 4 magic + 1 cmd + 4 len
const CHUNK_SIZE = 1024;

function buildFrame(cmd, payload) {
  const payloadBuf = payload ? Buffer.from(payload) : Buffer.alloc(0);
  const frame = Buffer.alloc(FRAME_HEADER_SIZE + payloadBuf.length);

  MAGIC.copy(frame, 0);
  frame[4] = cmd;
  frame.writeUInt32LE(payloadBuf.length, 5);
  payloadBuf.copy(frame, FRAME_HEADER_SIZE);

  return frame;
}

// PUSH_BEGIN payload: [total_size:4 LE] [filename:utf8]
function buildPushBeginFrame(totalSize, filename) {
  const nameBytes = Buffer.from(filename, "utf8");
  const payload = Buffer.alloc(4 + nameBytes.length);
  payload.writeUInt32LE(totalSize, 0);
  nameBytes.copy(payload, 4);
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

/**
 * Scan a byte buffer for the next complete frame (device log output may
 * surround it). Returns { cmd, payload, consumed } — `consumed` is the
 * offset just past the frame, for callers that keep the buffer — or null
 * when no complete frame is present yet.
 */
function parseResponse(buffer) {
  const magicIdx = buffer.indexOf(MAGIC);
  if (magicIdx === -1 || buffer.length - magicIdx < FRAME_HEADER_SIZE) {
    return null;
  }

  const cmd = buffer[magicIdx + 4];
  const payloadLen = buffer.readUInt32LE(magicIdx + 5);
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
  parseResponse,
};
