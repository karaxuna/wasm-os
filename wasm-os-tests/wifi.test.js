/**
 * Hardware test for the app-initiated "wifi" binding: pushes an
 * AssemblyScript app that connects to WiFi with the stored /littlefs/.env
 * credentials and makes an HTTP request to the web.
 *
 * Needs a connected board and real credentials in wasm-os-tests/.env (see
 * .env.example). Run:
 *
 *   npm run test:wifi
 *
 * Env:
 *   WASM_OS_PROFILE     build profile (default esp32s3-16mb-psram-oct)
 *   WASM_OS_SKIP_FLASH  set to 1 when the firmware is already flashed
 */
const { execSync } = require("child_process");
const path = require("path");
const fs = require("fs");
const { openPort, closePort, autoDetectPort } = require("wasm-os/src/serial");
const { FIXTURES_DIR, flashFirmware, pushFile, waitForMarker, hardReset } = require("./lib");

const OUTPUT_WASM = path.resolve(FIXTURES_DIR, "wifi-http.wasm");
const ENV_FILE = path.resolve(__dirname, ".env");

const PROFILE = process.env.WASM_OS_PROFILE || "esp32s3-16mb-psram-oct";
const SKIP_FLASH = process.env.WASM_OS_SKIP_FLASH === "1";

const FLASH_TIMEOUT = 300_000;
// wifi_connect retries up to 10 times before giving up; the app itself waits 30s.
const RUN_TIMEOUT = 90_000;

let portPath;

beforeAll(async () => {
  if (!fs.existsSync(ENV_FILE) || !fs.readFileSync(ENV_FILE, "utf8").includes("WIFI_SSID")) {
    throw new Error(`This test needs WiFi credentials: copy ${ENV_FILE}.example to ${ENV_FILE} and fill in WIFI_SSID/WIFI_PASS.`);
  }

  portPath = await autoDetectPort();
  if (!portPath) {
    throw new Error("No ESP32 device detected. Connect a board via USB.");
  }
  console.log(`Using serial port: ${portPath}`);
});

describe("wifi binding", () => {
  it(
    "flashes wasm-os",
    () => {
      if (SKIP_FLASH) {
        console.log("WASM_OS_SKIP_FLASH=1, skipping firmware flash");
        return;
      }
      flashFirmware(portPath, PROFILE, FLASH_TIMEOUT);
    },
    FLASH_TIMEOUT
  );

  it(
    "connects to wifi from the app and makes an HTTP request",
    async () => {
      console.log("Compiling AssemblyScript...");
      execSync(
        [
          "npx asc",
          path.resolve(FIXTURES_DIR, "wifi-http.ts"),
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
      const wasm = fs.readFileSync(OUTPUT_WASM);
      console.log(`Compiled WASM size: ${wasm.length} bytes`);

      // Give the device time to boot after flashing.
      await new Promise((r) => setTimeout(r, 5000));

      const port = await openPort(portPath);
      try {
        console.log("Pushing device settings (.env)...");
        await pushFile(port, fs.readFileSync(ENV_FILE), ".env");

        console.log("Pushing app...");
        await pushFile(port, wasm, "main.wasm");

        // Settings are only read at boot, so reboot before expecting markers.
        console.log("Rebooting...");
        await hardReset(port);

        const output = await waitForMarker(port, "wifi test app done", RUN_TIMEOUT);
        console.log("Device output:\n", output);
        expect(output).toContain("WIFI_CONNECTED");
        expect(output).toContain("HTTP_STATUS 200");
      } finally {
        await closePort(port);
      }
    },
    RUN_TIMEOUT + 60_000
  );
});

afterAll(() => {
  if (fs.existsSync(OUTPUT_WASM)) {
    fs.unlinkSync(OUTPUT_WASM);
  }
});
