#!/usr/bin/env node

const fs = require("fs");
const path = require("path");
const { program } = require("commander");
const { withDevice, resolvePort } = require("./device");
const { openNodeSerialTransport, listPorts, DEFAULT_BAUD } = require("wasm-os-sdk/src/transports/node-serial");
const { CHUNK_SIZE } = require("wasm-os-sdk/src/protocol");
const { connect, flashImage, hardReset, FLASH_BAUD } = require("./flash");

function portOptions(command) {
  return command
    .option("-p, --port <path>", "Serial port path (auto-detected if omitted)")
    .option("-b, --baud <rate>", "Baud rate", String(DEFAULT_BAUD));
}

function fail(err) {
  console.error(`\nError: ${err.message}`);
  process.exit(1);
}

async function pushFile(client, data, destName) {
  process.stdout.write("Starting transfer... ");
  let begun = false;

  const totalChunks = Math.ceil(data.length / CHUNK_SIZE);
  await client.pushFile(data, destName, {
    onProgress: (sent, total) => {
      if (!begun) {
        console.log("OK");
        begun = true;
      }
      const chunkNum = Math.ceil(sent / CHUNK_SIZE);
      const pct = Math.round((sent / total) * 100);
      process.stdout.write(`\rSending: ${chunkNum}/${totalChunks} chunks (${pct}%)`);
    },
  });

  console.log("\nFinalized. OK");
}

program
  .name("wasm-os")
  .description("CLI for pushing WebAssembly modules to ESP32 devices running wasm-os")
  .version("0.1.0");

portOptions(
  program
    .command("push")
    .description("Push a file to the device (main.wasm restarts the app)")
    .argument("<file>", "Path to file to push")
    .option("-n, --name <name>", "Destination filename on device (defaults to source filename)")
).action(async (file, opts) => {
  try {
    const filePath = path.resolve(file);
    if (!fs.existsSync(filePath)) {
      throw new Error(`File not found: ${filePath}`);
    }

    const data = fs.readFileSync(filePath);
    const destName = opts.name || path.basename(filePath);
    console.log(`File: ${filePath} (${data.length} bytes) -> ${destName}`);

    await withDevice(opts, (client) => pushFile(client, data, destName));
    console.log(`Push complete. ${destName} written to device.`);
  } catch (err) {
    fail(err);
  }
});

portOptions(
  program
    .command("delete")
    .alias("rm")
    .description("Delete files from the device filesystem")
    .argument("<files...>", "Filenames on the device")
).action(async (files, opts) => {
  try {
    await withDevice(opts, async (client) => {
      for (const name of files) {
        process.stdout.write(`Deleting ${name}... `);
        await client.deleteFile(name);
        console.log("OK");
      }
    });
  } catch (err) {
    fail(err);
  }
});

portOptions(program.command("restart").description("Restart the WASM module on the device")).action(async (opts) => {
  try {
    await withDevice(opts, async (client) => {
      process.stdout.write("Restarting WASM module... ");
      await client.restart();
      console.log("OK");
    });
  } catch (err) {
    fail(err);
  }
});

portOptions(program.command("monitor").description("Stream serial output from the device")).action(async (opts) => {
  try {
    const portPath = await resolvePort(opts);
    console.log(`Monitoring ${portPath} (Ctrl+C to exit)\n---`);
    const transport = await openNodeSerialTransport(portPath, parseInt(opts.baud, 10) || DEFAULT_BAUD);

    transport.subscribe((data) => process.stdout.write(data));
    transport.port.on("error", (err) => {
      console.error(`\nSerial error: ${err.message}`);
      process.exit(1);
    });
    transport.port.on("close", () => {
      console.log("\n--- Port closed");
      process.exit(0);
    });
    process.on("SIGINT", () => {
      console.log("\n--- Stopped");
      transport.close();
    });
  } catch (err) {
    fail(err);
  }
});

program
  .command("flash")
  .description("Flash a wasm-os firmware image to the device")
  .argument("<image>", "Path to a merged firmware .bin")
  .option("-p, --port <path>", "Serial port path (auto-detected if omitted)")
  .option("-b, --baud <rate>", "Flash baud rate", String(FLASH_BAUD))
  .option("-a, --address <offset>", "Flash offset (hex or decimal)", "0")
  .option("--erase", "Erase the whole flash first (also destroys littlefs and the pushed app)")
  .action(async (image, opts) => {
    try {
      const imagePath = path.resolve(image);
      if (!fs.existsSync(imagePath)) {
        throw new Error(`Image not found: ${imagePath}`);
      }

      const data = fs.readFileSync(imagePath);
      const address = parseInt(opts.address, opts.address.startsWith("0x") ? 16 : 10);
      if (Number.isNaN(address)) {
        throw new Error(`Invalid address: ${opts.address}`);
      }

      const portPath = await resolvePort(opts);
      console.log(`Port: ${portPath}`);
      console.log(`Image: ${imagePath} (${data.length} bytes) -> 0x${address.toString(16)}`);

      await flashImage(portPath, data, {
        address,
        baud: parseInt(opts.baud, 10) || FLASH_BAUD,
        eraseAll: Boolean(opts.erase),
      });
      console.log("Flash complete. Device restarted.");
    } catch (err) {
      fail(err);
    }
  });

program
  .command("info")
  .description("Detect the connected chip and its flash size")
  .option("-p, --port <path>", "Serial port path (auto-detected if omitted)")
  .action(async (opts) => {
    try {
      const portPath = await resolvePort(opts);
      const { loader, transport, chip } = await connect(portPath, {
        quiet: true,
      });

      try {
        const flashId = await loader.readFlashId();
        const sizeMb = Math.pow(2, (flashId >> 16) & 0xff) / (1024 * 1024);
        console.log(`Port:  ${portPath}`);
        console.log(`Chip:  ${chip}`);
        console.log(`Flash: ${sizeMb} MB`);
      } finally {
        // Reset out of the ROM bootloader, or the firmware stays down
        // until a power cycle.
        await loader.after();
        await transport.disconnect();
        await hardReset(portPath);
      }
    } catch (err) {
      fail(err);
    }
  });

program
  .command("ports")
  .description("List available serial ports")
  .action(async () => {
    const ports = await listPorts();

    if (ports.length === 0) {
      console.log("No serial ports found.");
      return;
    }
    for (const p of ports) {
      const info = [p.path];
      if (p.manufacturer) info.push(p.manufacturer);
      if (p.vendorId) info.push(`VID:${p.vendorId}`);
      if (p.productId) info.push(`PID:${p.productId}`);
      console.log(info.join("  "));
    }
  });

program.parse();
