// Browser transport on the Web Serial API (Chromium-only). Usage:
//
//   const serialPort = await navigator.serial.requestPort();
//   const transport = await openWebSerialTransport(serialPort);
//   const client = createDeviceClient(transport);

const DEFAULT_BAUD = 115200;

/**
 * Wrap a Web Serial `SerialPort` in the SDK transport interface, opening it
 * first when it is not open yet. The read pump starts immediately; data
 * arriving with no subscriber is dropped, matching the Node transport.
 */
async function openWebSerialTransport(serialPort, baudRate = DEFAULT_BAUD) {
  if (!serialPort.readable) {
    await serialPort.open({ baudRate });
  }

  const subscribers = new Set();
  const writer = serialPort.writable.getWriter();
  let reader = null;
  let closed = false;

  const pump = (async () => {
    while (!closed && serialPort.readable) {
      reader = serialPort.readable.getReader();
      try {
        while (true) {
          const { value, done } = await reader.read();
          if (done) {
            break;
          }
          if (value) {
            for (const cb of [...subscribers]) {
              cb(value);
            }
          }
        }
      } catch (err) {
        // Non-fatal stream errors (e.g. framing) end this reader; the outer
        // loop reacquires `readable` when the port is still usable.
        if (closed) {
          break;
        }
      } finally {
        reader.releaseLock();
        reader = null;
      }
    }
  })();

  return {
    write(bytes) {
      return writer.write(bytes);
    },
    subscribe(cb) {
      subscribers.add(cb);
      return () => {
        subscribers.delete(cb);
      };
    },
    setSignals({ dtr, rts }) {
      return serialPort.setSignals({ dataTerminalReady: dtr, requestToSend: rts });
    },
    async close() {
      closed = true;
      if (reader) {
        try {
          await reader.cancel();
        } catch (err) {
          // The port may already be gone (unplugged); closing proceeds.
        }
      }
      await pump;
      writer.releaseLock();
      await serialPort.close();
    },
  };
}

module.exports = {
  DEFAULT_BAUD,
  openWebSerialTransport,
};
