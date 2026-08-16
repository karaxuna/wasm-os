const { execSync } = require("child_process");
const path = require("path");
const fs = require("fs");
const { SerialPort } = require("serialport");
const {
  openPort,
  closePort,
  sendAndWaitForResponse,
  sendCommandWithRetry,
  PUSH_BEGIN_TIMEOUT,
  autoDetectPort,
} = require("../src/serial");
const { buildPushBeginFrame, buildPushDataFrame, buildPushEndFrame, CHUNK_SIZE } = require("../src/protocol");

const FIRMWARE_DIR = path.resolve(__dirname, "../..");
const FIXTURES_DIR = path.resolve(__dirname, "fixtures");
const OUTPUT_WASM = path.resolve(__dirname, "fixtures/output.wasm");

const IDF_PATH = process.env.IDF_PATH || "/Users/kakhaber/.espressif/v5.5.4/esp-idf";
const IDF_SHELL = `. ${IDF_PATH}/export.sh 2>/dev/null`;
// Match the profile to the connected board, e.g.:
//   WASM_OS_PROFILE=esp32-4mb npm run test:hw
const PROFILE = process.env.WASM_OS_PROFILE || "esp32s3-16mb-psram-oct";

const FLASH_TIMEOUT = 300_000;
const PUSH_TIMEOUT = 30_000;

function idf(cmd) {
  return execSync(`bash -c '${IDF_SHELL} && cd ${FIRMWARE_DIR} && ${cmd}'`, {
    stdio: "inherit",
    timeout: FLASH_TIMEOUT,
  });
}

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
      idf(`idf.py @profiles/${PROFILE} -p ${portPath} build flash`);
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
        // Opening the port resets auto-reset boards; retry until the device answers.
        await sendCommandWithRetry(port, buildPushBeginFrame(wasm.length, "main.wasm"), {
          timeout: PUSH_BEGIN_TIMEOUT,
        });

        for (let i = 0; i < wasm.length; i += CHUNK_SIZE) {
          const chunk = wasm.subarray(i, Math.min(i + CHUNK_SIZE, wasm.length));
          await sendAndWaitForResponse(port, buildPushDataFrame(chunk));
        }

        await sendAndWaitForResponse(port, buildPushEndFrame());

        // Listen for serial output to confirm the app ran
        output = await new Promise((resolve) => {
          let buf = "";
          const timeout = setTimeout(() => {
            port.off("data", onData);
            resolve(buf);
          }, 10_000);

          const onData = (data) => {
            buf += data.toString();
            if (buf.includes("wasm-os test app done")) {
              clearTimeout(timeout);
              port.off("data", onData);
              resolve(buf);
            }
          };

          port.on("data", onData);
        });
      } finally {
        await closePort(port);
      }

      console.log("Device output:\n", output);
      expect(output).toContain("wasm-os test app started");
      expect(output).toContain("wasm-os test app done");
    },
    PUSH_TIMEOUT + 15_000
  );
});

afterAll(() => {
  if (fs.existsSync(OUTPUT_WASM)) {
    fs.unlinkSync(OUTPUT_WASM);
  }
});
