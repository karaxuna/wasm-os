const { openPort, closePort, autoDetectPort, DEFAULT_BAUD } = require("./serial");

/**
 * Resolve the serial port path from command options (or auto-detection).
 * Throws when no device can be found.
 */
async function resolvePort(opts) {
  const portPath = opts.port || (await autoDetectPort());
  if (!portPath) {
    throw new Error("No ESP32 device found. Use --port to specify manually.");
  }
  return portPath;
}

/**
 * Open the device, run `fn(port)`, and always close the port afterwards —
 * also on failure, so a crashed command never wedges the serial device.
 */
async function withDevice(opts, fn) {
  const portPath = await resolvePort(opts);
  console.log(`Port: ${portPath}`);

  const port = await openPort(portPath, parseInt(opts.baud, 10) || DEFAULT_BAUD);
  try {
    return await fn(port);
  } finally {
    await closePort(port);
  }
}

module.exports = { resolvePort, withDevice };
