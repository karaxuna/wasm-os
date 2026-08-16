/**
 * Shared plumbing for the hardware suites: firmware location, flashing, and
 * device access through @wasm-os/sdk (Node transport + device client).
 */
const { execSync } = require("child_process");
const path = require("path");
const { createDeviceClient, hardReset } = require("@wasm-os/sdk/src/client");
const { openNodeSerialTransport, autoDetectPort, listPorts } = require("@wasm-os/sdk/src/transports/node-serial");

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

/** Open the port and wrap it in an SDK transport + device client. */
async function openDevice(portPath) {
  const transport = await openNodeSerialTransport(portPath);
  const client = createDeviceClient(transport);
  return { transport, client };
}

/** Resolve when `marker` shows up in the serial stream; reject on timeout. */
function waitForMarker(transport, marker, timeoutMs) {
  return new Promise((resolve, reject) => {
    let buf = "";
    let unsubscribe = () => {};

    const timer = setTimeout(() => {
      unsubscribe();
      reject(new Error(`Timed out after ${timeoutMs / 1000}s waiting for "${marker}". Device output:\n${buf}`));
    }, timeoutMs);

    unsubscribe = transport.subscribe((data) => {
      buf += data.toString();
      if (buf.includes(marker)) {
        clearTimeout(timer);
        unsubscribe();
        resolve(buf);
      }
    });
  });
}

module.exports = {
  FIRMWARE_DIR,
  FIXTURES_DIR,
  IDF_PATH,
  flashFirmware,
  openDevice,
  waitForMarker,
  hardReset,
  autoDetectPort,
  listPorts,
};
