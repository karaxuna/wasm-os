const crypto = require("crypto");
const { SerialPort } = require("serialport");
const { openSerialDevice } = require("./webserial");

const FLASH_BAUD = 921600;
const ROM_BAUD = 115200;

/**
 * esptool-js ships an unusable lib/ — its published sources use extensionless
 * relative imports that Node's ESM resolver rejects. bundle.js is the
 * self-contained build, reachable only through a dynamic import from CJS.
 */
async function loadEsptool() {
  return import("esptool-js/bundle.js");
}

function createTerminal(quiet) {
  return {
    clean() {},
    writeLine(data) {
      if (!quiet) {
        console.log(data);
      }
    },
    write(data) {
      if (!quiet) {
        process.stdout.write(data);
      }
    },
  };
}

/**
 * Connect to the ROM bootloader and return the loader plus its transport. The
 * caller owns teardown so a failed flash still resets and releases the port.
 */
async function connect(portPath, { baud = FLASH_BAUD, quiet = false } = {}) {
  const { ESPLoader, Transport } = await loadEsptool();

  const device = await openSerialDevice(portPath);
  const transport = new Transport(device, false);

  // esptool-js 0.6.1 calls trace() unconditionally on disconnect even when
  // tracing is off, printing TRACE lines into normal CLI output.
  transport.trace = () => {};

  const loader = new ESPLoader({
    transport,
    baudrate: baud,
    romBaudrate: ROM_BAUD,
    terminal: createTerminal(quiet),
  });

  const chip = await loader.main();

  return { loader, transport, chip };
}

/**
 * Pulse EN via RTS with IO0 (DTR) released so the chip reboots into the
 * application. esptool-js's own hard_reset does not reliably leave the ROM
 * bootloader on USB-Serial-JTAG consoles (P4/S3), stranding the device until
 * a power cycle.
 */
function hardReset(portPath) {
  return new Promise((resolve, reject) => {
    const port = new SerialPort({ path: portPath, baudRate: 115200 }, (err) => {
      if (err) {
        return reject(err);
      }

      port.set({ dtr: false, rts: true }, () => {
        setTimeout(() => {
          port.set({ dtr: false, rts: false }, () => {
            port.close(() => {
              return resolve();
            });
          });
        }, 150);
      });
    });
  });
}

/**
 * Flash every `{ data, address }` segment in one session. The merged image
 * covers bootloader, partition table and app0, all of which sit below the
 * littlefs partition, so a normal flash leaves the pushed .wasm app intact.
 * `eraseAll` wipes it.
 */
async function flashSegments(portPath, segments, { baud = FLASH_BAUD, eraseAll = false, eraseRegions = [] } = {}) {
  const { loader, transport, chip } = await connect(portPath, { baud });

  try {
    console.log(`Chip: ${chip}`);

    if (eraseAll) {
      process.stdout.write("Erasing flash... ");
      await loader.eraseFlash();
      console.log("OK");
    }

    /* Regions rewritten as sparse segments (littlefs) must be erased whole
     * first: a stale metadata block left over from a previous filesystem can
     * out-revision the freshly written half of a littlefs metadata pair. */
    for (const region of eraseRegions) {
      process.stdout.write(`Erasing 0x${region.size.toString(16)} bytes at 0x${region.offset.toString(16)}... `);
      await loader.eraseRegion(region.offset, region.size);
      console.log("OK");
    }

    await loader.writeFlash({
      fileArray: segments.map((s) => {
        return { data: new Uint8Array(s.data), address: s.address };
      }),
      flashSize: "keep",
      flashMode: "keep",
      flashFreq: "keep",
      eraseAll: false,
      compress: true,
      calculateMD5Hash: (data) => {
        return crypto.createHash("md5").update(Buffer.from(data)).digest("hex");
      },
    });
    console.log("");
    await loader.after();
  } finally {
    await transport.disconnect();
    await hardReset(portPath);
  }
}

module.exports = { connect, flashSegments, hardReset, FLASH_BAUD };
