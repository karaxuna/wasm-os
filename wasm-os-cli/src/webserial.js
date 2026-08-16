const { SerialPort } = require("serialport");

/**
 * macOS exposes each device as both /dev/tty.* and /dev/cu.*, but
 * SerialPort.list() only reports the tty form. Compare on the suffix so the
 * USB vendor/product IDs are still found for a /dev/cu.* path — esptool picks
 * its reset sequence from the product ID, so an empty match breaks flashing.
 */
function sameDevice(a, b) {
  const strip = (p) => {
    return p.replace(/^\/dev\/(tty|cu)\./, "");
  };

  return a === b || strip(a) === strip(b);
}

function createReadable(port) {
  return new ReadableStream({
    start(controller) {
      // The consumer may cancel the stream independently of the port, so every
      // controller call has to tolerate an already-closed controller.
      let closed = false;

      const stop = () => {
        if (closed) {
          return;
        }

        closed = true;
        try {
          controller.close();
        } catch {
          closed = true;
        }
      };

      port.on("data", (chunk) => {
        if (closed) {
          return;
        }

        try {
          controller.enqueue(new Uint8Array(chunk));
        } catch {
          closed = true;
        }
      });

      port.on("end", stop);
      port.on("close", stop);
      port.on("error", (err) => {
        if (closed) {
          return;
        }

        closed = true;
        try {
          controller.error(err);
        } catch {
          closed = true;
        }
      });
    },
  });
}

/**
 * Node's Writable.toWeb() never drains serialport's buffer, so frames queue up
 * without reaching the chip and every command times out. Flush each write.
 */
function createWritable(port) {
  return new WritableStream({
    write(chunk) {
      return new Promise((resolve, reject) => {
        port.write(Buffer.from(chunk), (err) => {
          if (err) {
            return reject(err);
          }

          port.drain((drainErr) => {
            return drainErr ? reject(drainErr) : resolve();
          });
        });
      });
    },
  });
}

/**
 * Minimal Web Serial adapter over node-serialport. esptool-js is written
 * against the browser Web Serial API and only touches open/close/readable/
 * writable/setSignals/getInfo.
 */
function createSerialDevice(path, info = {}) {
  let port = null;
  // serialport's set() resets any flag that is not passed, so both signals must
  // be tracked and always written together — otherwise the reset sequence
  // silently toggles the line it did not mean to touch.
  const signals = { dtr: false, rts: false };

  const open = (options = {}) => {
    return new Promise((resolve, reject) => {
      port = new SerialPort({
        path,
        baudRate: options.baudRate || 115200,
        dataBits: options.dataBits || 8,
        stopBits: options.stopBits || 1,
        parity: options.parity || "none",
        autoOpen: false,
      });

      port.open((err) => {
        if (err) {
          return reject(new Error(`Failed to open ${path}: ${err.message}`));
        }

        device.readable = createReadable(port);
        device.writable = createWritable(port);
        resolve();
      });
    });
  };

  const setSignals = (next) => {
    if (next.dataTerminalReady !== undefined) {
      signals.dtr = next.dataTerminalReady;
    }

    if (next.requestToSend !== undefined) {
      signals.rts = next.requestToSend;
    }

    return new Promise((resolve, reject) => {
      port.set({ dtr: signals.dtr, rts: signals.rts }, (err) => {
        return err ? reject(err) : resolve();
      });
    });
  };

  const close = () => {
    return new Promise((resolve) => {
      device.readable = null;
      device.writable = null;

      if (!port || !port.isOpen) {
        return resolve();
      }

      port.close(() => {
        return resolve();
      });
    });
  };

  const getInfo = () => {
    return info;
  };

  const device = { path, readable: null, writable: null, open, setSignals, close, getInfo };

  return device;
}

/** Build a device for `path`, attaching its USB vendor/product IDs. */
async function openSerialDevice(path) {
  const ports = await SerialPort.list();
  const match = ports.find((p) => {
    return sameDevice(p.path, path);
  });

  const info = {};
  if (match && match.vendorId) {
    info.usbVendorId = parseInt(match.vendorId, 16);
  }

  if (match && match.productId) {
    info.usbProductId = parseInt(match.productId, 16);
  }

  return createSerialDevice(path, info);
}

module.exports = { createSerialDevice, openSerialDevice };
