// Node transport on node-serialport. "serialport" is required lazily so
// browser bundles importing the SDK never touch it.

const DEFAULT_BAUD = 115200;

function nodeSerialPort() {
  const { SerialPort } = require("serialport");
  return SerialPort;
}

function listPorts() {
  return nodeSerialPort().list();
}

/** Pick the most likely ESP32 serial port, or null when none is present. */
async function autoDetectPort() {
  const ports = await listPorts();

  const espPorts = ports.filter((p) => {
    const vid = (p.vendorId || "").toLowerCase();
    const manufacturer = (p.manufacturer || "").toLowerCase();
    return (
      vid === "10c4" || // CP210x
      vid === "0403" || // FTDI
      vid === "1a86" || // CH340
      vid === "303a" || // Espressif USB-JTAG
      manufacturer.includes("silicon") ||
      manufacturer.includes("ftdi") ||
      manufacturer.includes("espressif")
    );
  });

  if (espPorts.length === 0) {
    return null;
  }

  // Prefer Espressif's own USB-JTAG/serial over third-party bridges.
  const espressif = espPorts.find((p) => (p.vendorId || "").toLowerCase() === "303a");
  let portPath = (espressif || espPorts[0]).path;

  // On macOS prefer /dev/cu.* — /dev/tty.* blocks until carrier detect.
  if (process.platform === "darwin" && portPath.includes("/dev/tty.")) {
    portPath = portPath.replace("/dev/tty.", "/dev/cu.");
  }

  return portPath;
}

/**
 * Open `path` and wrap it in the SDK transport interface. The underlying
 * node-serialport instance is exposed as `.port` for host-specific needs
 * (error events, exotic control lines); protocol code must not use it.
 */
function openNodeSerialTransport(path, baudRate = DEFAULT_BAUD) {
  const SerialPort = nodeSerialPort();

  return new Promise((resolve, reject) => {
    const port = new SerialPort({ path, baudRate }, (err) => {
      if (err) {
        reject(new Error(`Failed to open ${path}: ${err.message}`));
        return;
      }

      resolve({
        write(bytes) {
          return new Promise((res, rej) => {
            const buf = Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength);
            port.write(buf, (writeErr) => {
              if (writeErr) {
                rej(writeErr);
              } else {
                res();
              }
            });
          });
        },
        subscribe(cb) {
          const onData = (data) => {
            cb(data);
          };
          port.on("data", onData);
          return () => {
            port.off("data", onData);
          };
        },
        setSignals({ dtr, rts }) {
          return new Promise((res) => {
            port.set({ dtr, rts }, () => {
              res();
            });
          });
        },
        close() {
          return new Promise((res) => {
            if (!port.isOpen) {
              res();
              return;
            }
            port.close(() => {
              res();
            });
          });
        },
        port,
      });
    });
  });
}

module.exports = {
  DEFAULT_BAUD,
  listPorts,
  autoDetectPort,
  openNodeSerialTransport,
};
