const { openNodeSerialTransport, autoDetectPort, DEFAULT_BAUD } = require("@wasm-os/sdk/src/transports/node-serial");
const { createDeviceClient } = require("@wasm-os/sdk/src/client");

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
 * Open the device, run `fn(client, transport)`, and always close the port
 * afterwards — also on failure, so a crashed command never wedges the
 * serial device.
 */
async function withDevice(opts, fn) {
  const portPath = await resolvePort(opts);
  console.log(`Port: ${portPath}`);

  const transport = await openNodeSerialTransport(portPath, parseInt(opts.baud, 10) || DEFAULT_BAUD);
  const client = createDeviceClient(transport);
  try {
    return await fn(client, transport);
  } finally {
    await transport.close();
  }
}

module.exports = { resolvePort, withDevice };
