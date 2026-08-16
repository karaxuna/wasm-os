const { execSync } = require("child_process");
const path = require("path");
const fs = require("fs");
const { SerialPort } = require("serialport");
const { openPort, closePort, autoDetectPort } = require("wasm-os/src/serial");
const { FIXTURES_DIR, flashFirmware, pushFile, waitForMarker } = require("./lib");

const OUTPUT_WASM = path.resolve(FIXTURES_DIR, "output.wasm");

// Match the profile to the connected board, e.g.:
//   WASM_OS_PROFILE=esp32-4mb npm run test:hw
const PROFILE = process.env.WASM_OS_PROFILE || "esp32s3-16mb-psram-oct";

const FLASH_TIMEOUT = 300_000;
const PUSH_TIMEOUT = 30_000;
const RUN_TIMEOUT = 15_000;

let portPath;

beforeAll(async () => {
  portPath = await autoDetectPort();
  if (!portPath) {
    const ports = await SerialPort.list();
    console.log("Available ports:", ports.map((p) => p.path));
    throw new Error("No ESP32 device detected. Connect a board via USB.");
  }
  console.log(`Using serial port: ${portPath}`);
});

describe("flash firmware", () => {
  it(
    "should build and flash wasm-os to the device",
    () => {
      console.log("Building and flashing firmware...");
      flashFirmware(portPath, PROFILE, FLASH_TIMEOUT);
    },
    FLASH_TIMEOUT
  );
});

describe("push wasm", () => {
  it(
    "should compile AssemblyScript to wasm and push to device",
    async () => {
      // Compile AssemblyScript
      console.log("Compiling AssemblyScript...");
      execSync(
        [
          "npx asc",
          path.resolve(FIXTURES_DIR, "main.ts"),
          "-o",
          OUTPUT_WASM,
          "--stackSize 3072",
          "--initialMemory 1",
          "--maximumMemory 1",
          "--noAssert",
          "--importMemory",
          "--exportRuntime",
        ].join(" "),
        { cwd: FIXTURES_DIR, stdio: "inherit", timeout: 30_000 }
      );

      expect(fs.existsSync(OUTPUT_WASM)).toBe(true);
      const wasm = fs.readFileSync(OUTPUT_WASM);
      console.log(`Compiled WASM size: ${wasm.length} bytes`);
      expect(wasm.length).toBeGreaterThan(0);

      // Wait for device to boot after flash
      console.log("Waiting for device to boot...");
      await new Promise((r) => setTimeout(r, 5000));

      // Push to device
      console.log("Pushing WASM to device...");
      const port = await openPort(portPath);

      let output;
      try {
        await pushFile(port, wasm, "main.wasm");

        // Listen for serial output to confirm the app ran
        output = await waitForMarker(port, "wasm-os test app done", RUN_TIMEOUT);
      } finally {
        await closePort(port);
      }

      console.log("Device output:\n", output);
      expect(output).toContain("wasm-os test app started");
      expect(output).toContain("wasm-os test app done");
    },
    PUSH_TIMEOUT + RUN_TIMEOUT + 15_000
  );
});

afterAll(() => {
  if (fs.existsSync(OUTPUT_WASM)) {
    fs.unlinkSync(OUTPUT_WASM);
  }
});
