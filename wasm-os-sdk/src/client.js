// Transport-agnostic device client: the command flows of the wasm-os serial
// protocol on top of any transport implementing
//   { write(bytes) -> Promise, subscribe(cb) -> unsubscribe,
//     setSignals({dtr, rts}) -> Promise, close() -> Promise }

const {
  RSP,
  CHUNK_SIZE,
  buildPushBeginFrame,
  buildPushDataFrame,
  buildPushEndFrame,
  buildRestartFrame,
  buildDeleteFrame,
  parseResponse,
} = require("./protocol");

const RESPONSE_TIMEOUT = 10000;

/**
 * PUSH_BEGIN stops the running apps before it ACKs, and the device allows a
 * spinning guest APP_STOP_TIMEOUT_MS (10s in wasm-os-core/main/app_runtime.c)
 * to tear down before force-deleting it. Wait past that bound, or pushing
 * over a busy app times out on a device that is behaving correctly.
 */
const PUSH_BEGIN_TIMEOUT = 15000;

const textDecoder = new TextDecoder();

function concatBytes(a, b) {
  const out = new Uint8Array(a.length + b.length);
  out.set(a, 0);
  out.set(b, a.length);
  return out;
}

function createDeviceClient(transport) {
  /**
   * Write a frame and wait for the device's ACK/NAK. Device log output may be
   * interleaved with the response, so incoming bytes are accumulated and
   * scanned for the framed reply.
   */
  function sendFrame(frame, timeout = RESPONSE_TIMEOUT) {
    return new Promise((resolve, reject) => {
      let buffer = new Uint8Array(0);
      let timer;
      let unsubscribe = () => {};

      const cleanup = () => {
        clearTimeout(timer);
        unsubscribe();
      };

      unsubscribe = transport.subscribe((data) => {
        buffer = concatBytes(buffer, data);
        const response = parseResponse(buffer);
        if (!response) {
          return;
        }
        cleanup();

        if (response.cmd === RSP.ACK) {
          resolve(response.payload);
        } else if (response.cmd === RSP.NAK) {
          const message = response.payload.length > 0 ? textDecoder.decode(response.payload) : "Device rejected command";
          reject(new Error(message));
        } else {
          reject(new Error(`Unexpected response: 0x${response.cmd.toString(16)}`));
        }
      });

      timer = setTimeout(() => {
        cleanup();
        reject(new Error("Response timeout"));
      }, timeout);

      Promise.resolve(transport.write(frame)).catch((err) => {
        cleanup();
        reject(new Error(`Write failed: ${err.message}`));
      });
    });
  }

  /**
   * sendFrame with retries on timeout. Opening the port resets boards with
   * USB-UART auto-reset wiring (CH340/CP210x), so the first frame can land
   * while the device is still booting — retry until it answers.
   */
  async function sendFrameWithRetry(frame, { attempts = 3, timeout = 5000 } = {}) {
    let lastError;
    for (let i = 0; i < attempts; i++) {
      try {
        return await sendFrame(frame, timeout);
      } catch (err) {
        if (!/timeout/i.test(err.message)) {
          throw err;
        }
        lastError = err;
      }
    }
    throw lastError;
  }

  /**
   * Stream a file to /littlefs/<name>. `onProgress(sent, total)` fires after
   * every chunk; `beginAttempts` overrides the PUSH_BEGIN retry count.
   */
  async function pushFile(data, name, { onProgress, beginAttempts = 3 } = {}) {
    await sendFrameWithRetry(buildPushBeginFrame(data.length, name), {
      attempts: beginAttempts,
      timeout: PUSH_BEGIN_TIMEOUT,
    });

    for (let offset = 0; offset < data.length; offset += CHUNK_SIZE) {
      const chunk = data.subarray(offset, Math.min(offset + CHUNK_SIZE, data.length));
      await sendFrame(buildPushDataFrame(chunk));
      if (onProgress) {
        onProgress(offset + chunk.length, data.length);
      }
    }

    await sendFrame(buildPushEndFrame());
  }

  function deleteFile(name, options) {
    return sendFrameWithRetry(buildDeleteFrame(name), options);
  }

  function restart(options) {
    return sendFrameWithRetry(buildRestartFrame(), options);
  }

  return {
    sendFrame,
    sendFrameWithRetry,
    pushFile,
    deleteFile,
    restart,
  };
}

/**
 * Pulse EN via RTS while leaving IO0 (DTR) high, so the chip reboots into
 * the application rather than the ROM downloader. Device settings (.env) are
 * only read at boot, so pushing them is pointless without this.
 */
async function hardReset(transport) {
  await transport.setSignals({ dtr: false, rts: true });
  await new Promise((resolve) => {
    setTimeout(resolve, 150);
  });
  await transport.setSignals({ dtr: false, rts: false });
}

module.exports = {
  createDeviceClient,
  hardReset,
  RESPONSE_TIMEOUT,
  PUSH_BEGIN_TIMEOUT,
};
