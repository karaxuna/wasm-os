const { SerialPort } = require("serialport");
const { RSP, parseResponse } = require("./protocol");

const DEFAULT_BAUD = 115200;
const RESPONSE_TIMEOUT = 10000;

/**
 * PUSH_BEGIN stops the running app before it ACKs, and the device allows a
 * spinning guest APP_STOP_TIMEOUT_MS (10s in main/app_runtime.c) to tear down
 * before force-deleting it. Wait past that bound, or pushing over a busy app
 * times out on a device that is behaving correctly.
 */
const PUSH_BEGIN_TIMEOUT = 15000;

function openPort(path, baudRate = DEFAULT_BAUD) {
  return new Promise((resolve, reject) => {
    const port = new SerialPort({ path, baudRate }, (err) => {
      if (err) {
        reject(new Error(`Failed to open ${path}: ${err.message}`));
      } else {
        resolve(port);
      }
    });
  });
}

function closePort(port) {
  return new Promise((resolve) => {
    if (!port.isOpen) {
      resolve();
      return;
    }
    port.close(() => resolve());
  });
}

/**
 * Write a frame and wait for the device's ACK/NAK. Device log output may be
 * interleaved with the response, so incoming bytes are accumulated and
 * scanned for the framed reply.
 */
function sendAndWaitForResponse(port, frame, timeout = RESPONSE_TIMEOUT) {
  return new Promise((resolve, reject) => {
    let buffer = Buffer.alloc(0);
    let timer;

    const cleanup = () => {
      clearTimeout(timer);
      port.off("data", onData);
      port.off("error", onError);
    };

    const onData = (data) => {
      buffer = Buffer.concat([buffer, data]);
      const response = parseResponse(buffer);
      if (!response) {
        return;
      }
      cleanup();

      if (response.cmd === RSP.ACK) {
        resolve(response.payload);
      } else if (response.cmd === RSP.NAK) {
        const message = response.payload.length > 0 ? response.payload.toString("utf8") : "Device rejected command";
        reject(new Error(message));
      } else {
        reject(new Error(`Unexpected response: 0x${response.cmd.toString(16)}`));
      }
    };

    const onError = (err) => {
      cleanup();
      reject(err);
    };

    timer = setTimeout(() => {
      cleanup();
      reject(new Error("Response timeout"));
    }, timeout);

    port.on("data", onData);
    port.on("error", onError);

    port.write(frame, (err) => {
      if (err) {
        cleanup();
        reject(new Error(`Write failed: ${err.message}`));
      }
    });
  });
}

/**
 * sendAndWaitForResponse with retries on timeout. Opening the port resets
 * boards with USB-UART auto-reset wiring (CH340/CP210x), so the first frame
 * can land while the device is still booting — retry until it answers.
 */
async function sendCommandWithRetry(port, frame, { attempts = 3, timeout = 5000 } = {}) {
  let lastError;
  for (let i = 0; i < attempts; i++) {
    try {
      return await sendAndWaitForResponse(port, frame, timeout);
    } catch (err) {
      if (!/timeout/i.test(err.message)) {
        throw err;
      }
      lastError = err;
    }
  }
  throw lastError;
}

/** Pick the most likely ESP32 serial port, or null when none is present. */
async function autoDetectPort() {
  const ports = await SerialPort.list();

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

module.exports = {
  openPort,
  closePort,
  sendAndWaitForResponse,
  sendCommandWithRetry,
  autoDetectPort,
  DEFAULT_BAUD,
  PUSH_BEGIN_TIMEOUT,
};
