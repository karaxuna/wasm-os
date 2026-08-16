/**
 * Shared plumbing for the hardware suites: firmware location, flashing, the
 * serial push protocol, and marker-based assertions on device output.
 */
const { execSync } = require("child_process");
const path = require("path");
const { sendAndWaitForResponse, sendCommandWithRetry, PUSH_BEGIN_TIMEOUT } = require("wasm-os/src/serial");
const { buildPushBeginFrame, buildPushDataFrame, buildPushEndFrame, CHUNK_SIZE } = require("wasm-os/src/protocol");

const FIRMWARE_DIR = path.resolve(__dirname, "../wasm-os-core");
const FIXTURES_DIR = path.resolve(__dirname, "fixtures");
const IDF_PATH = process.env.IDF_PATH || "/Users/kakhaber/.espressif/v5.5.4/esp-idf";

/** Build the firmware with the given profile and flash it to the device. */
function flashFirmware(portPath, profile, timeoutMs) {
  execSync(
    `bash -c '. ${IDF_PATH}/export.sh 2>/dev/null && cd ${FIRMWARE_DIR} && idf.py @profiles/${profile} -p ${portPath} build flash'`,
    { stdio: "inherit", timeout: timeoutMs }
  );
}

/**
 * Stream a file to /littlefs/<name> over the serial protocol. Opening the
 * port resets auto-reset boards, so PUSH_BEGIN retries until the device
 * answers; `options` is forwarded to sendCommandWithRetry to override that.
 */
async function pushFile(port, data, name, options = {}) {
  await sendCommandWithRetry(port, buildPushBeginFrame(data.length, name), {
    timeout: PUSH_BEGIN_TIMEOUT,
    ...options,
  });
  for (let i = 0; i < data.length; i += CHUNK_SIZE) {
    await sendAndWaitForResponse(port, buildPushDataFrame(data.subarray(i, Math.min(i + CHUNK_SIZE, data.length))));
  }
  await sendAndWaitForResponse(port, buildPushEndFrame());
}

/** Resolve when `marker` shows up in the serial stream; reject on timeout. */
function waitForMarker(port, marker, timeoutMs) {
  return new Promise((resolve, reject) => {
    let buf = "";
    const timer = setTimeout(() => {
      port.off("data", onData);
      reject(new Error(`Timed out after ${timeoutMs / 1000}s waiting for "${marker}". Device output:\n${buf}`));
    }, timeoutMs);

    const onData = (data) => {
      buf += data.toString();
      if (buf.includes(marker)) {
        clearTimeout(timer);
        port.off("data", onData);
        resolve(buf);
      }
    };

    port.on("data", onData);
  });
}

/**
 * Pulse EN via RTS while leaving IO0 (DTR) high, so the chip reboots into the
 * application rather than the ROM downloader. Device settings (.env) are only
 * read at boot, so pushing them is pointless without this.
 */
function hardReset(port) {
  return new Promise((resolve) => {
    port.set({ dtr: false, rts: true }, () => {
      setTimeout(() => {
        port.set({ dtr: false, rts: false }, () => {
          return resolve();
        });
      }, 150);
    });
  });
}

module.exports = {
  FIRMWARE_DIR,
  FIXTURES_DIR,
  IDF_PATH,
  flashFirmware,
  pushFile,
  waitForMarker,
  hardReset,
};
