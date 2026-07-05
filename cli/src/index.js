#!/usr/bin/env node

const fs = require("fs");
const path = require("path");
const { program } = require("commander");
const { withDevice, resolvePort } = require("./device");
const { openPort, sendAndWaitForResponse, sendCommandWithRetry, DEFAULT_BAUD } = require("./serial");
const {
  buildPushBeginFrame,
  buildPushDataFrame,
  buildPushEndFrame,
  buildRestartFrame,
  CHUNK_SIZE,
} = require("./protocol");

function portOptions(command) {
  return command
    .option("-p, --port <path>", "Serial port path (auto-detected if omitted)")
    .option("-b, --baud <rate>", "Baud rate", String(DEFAULT_BAUD));
}

function fail(err) {
  console.error(`\nError: ${err.message}`);
  process.exit(1);
}

async function pushFile(port, data, destName) {
  process.stdout.write("Starting transfer... ");
  await sendCommandWithRetry(port, buildPushBeginFrame(data.length, destName));
  console.log("OK");

  const totalChunks = Math.ceil(data.length / CHUNK_SIZE);
  for (let offset = 0; offset < data.length; offset += CHUNK_SIZE) {
    const chunk = data.subarray(offset, Math.min(offset + CHUNK_SIZE, data.length));
    const chunkNum = Math.floor(offset / CHUNK_SIZE) + 1;
    const pct = Math.round(((offset + chunk.length) / data.length) * 100);
    process.stdout.write(`\rSending: ${chunkNum}/${totalChunks} chunks (${pct}%)`);
    await sendAndWaitForResponse(port, buildPushDataFrame(chunk));
  }
  console.log("");

  process.stdout.write("Finalizing... ");
  await sendAndWaitForResponse(port, buildPushEndFrame());
  console.log("OK");
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

    await withDevice(opts, (port) => pushFile(port, data, destName));
    console.log(`Push complete. ${destName} written to device.`);
  } catch (err) {
    fail(err);
  }
});

portOptions(program.command("restart").description("Restart the WASM module on the device")).action(async (opts) => {
  try {
    await withDevice(opts, async (port) => {
      process.stdout.write("Restarting WASM module... ");
      await sendCommandWithRetry(port, buildRestartFrame());
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
    const port = await openPort(portPath, parseInt(opts.baud, 10) || DEFAULT_BAUD);

    port.on("data", (data) => process.stdout.write(data));
    port.on("error", (err) => {
      console.error(`\nSerial error: ${err.message}`);
      process.exit(1);
    });
    port.on("close", () => {
      console.log("\n--- Port closed");
      process.exit(0);
    });
    process.on("SIGINT", () => {
      console.log("\n--- Stopped");
      port.close();
    });
  } catch (err) {
    fail(err);
  }
});

program
  .command("ports")
  .description("List available serial ports")
  .action(async () => {
    const { SerialPort } = require("serialport");
    const ports = await SerialPort.list();

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
