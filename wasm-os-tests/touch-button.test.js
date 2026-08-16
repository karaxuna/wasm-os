/**
 * Interactive touch test for the HW-458 board (ESP32-2432S028 "CYD":
 * classic ESP32 + ILI9341 SPI display + XPT2046 resistive touch).
 *
 * Flashes wasm-os, pushes a plain-C app (compiled with wasi-sdk) that draws
 * a large green button, then waits for a HUMAN to press it on the screen.
 * Not part of `npm test` — run:
 *
 *   npm run test:touch
 *
 * Env:
 *   WASM_OS_PROFILE     build profile (default esp32-4mb, matches the CYD)
 *   WASM_OS_SKIP_FLASH  set to 1 when the firmware is already flashed
 */
const { execSync } = require("child_process");
const path = require("path");
const fs = require("fs");
const { openPort, closePort, autoDetectPort } = require("wasm-os/src/serial");
const { FIXTURES_DIR, flashFirmware, pushFile, waitForMarker } = require("./lib");

const FIXTURE_DIR = path.join(FIXTURES_DIR, "touch-button");
const APP_WASM = path.join(FIXTURE_DIR, "app.wasm");

const PROFILE = process.env.WASM_OS_PROFILE || "esp32-4mb";
const SKIP_FLASH = process.env.WASM_OS_SKIP_FLASH === "1";

const FLASH_TIMEOUT = 300_000;
const BUILD_TIMEOUT = 600_000; // first run downloads wasi-sdk
const READY_TIMEOUT = 60_000;
const HUMAN_TIMEOUT = 60_000;

let portPath;

beforeAll(async () => {
  portPath = await autoDetectPort();
  if (!portPath) {
    throw new Error("No ESP32 device detected. Connect the HW-458 board via USB.");
  }
  console.log(`Using serial port: ${portPath}`);
});

describe("touch button (human in the loop)", () => {
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
    "builds the app, pushes it, and waits for a human button press",
    async () => {
      console.log("Building touch-button fixture (first run downloads wasi-sdk)...");
      execSync(`${path.join(FIXTURE_DIR, "build.sh")}`, {
        stdio: "inherit",
        timeout: BUILD_TIMEOUT,
      });

      const wasm = fs.readFileSync(APP_WASM);
      console.log(`App size: ${wasm.length} bytes`);

      // Give the device time to boot after flashing.
      await new Promise((r) => setTimeout(r, 3000));

      const port = await openPort(portPath);
      try {
        console.log("Pushing app...");
        await pushFile(port, wasm, "main.wasm");

        console.log("Waiting for the app to draw its UI...");
        await waitForMarker(port, "UI_READY", READY_TIMEOUT);

        console.log("\n" + "=".repeat(60));
        console.log("👉  PRESS THE GREEN BUTTON ON THE TOUCHSCREEN NOW");
        console.log(`    (you have ${HUMAN_TIMEOUT / 1000} seconds; it turns blue when registered)`);
        console.log("=".repeat(60) + "\n");

        await waitForMarker(port, "BUTTON_PRESSED", HUMAN_TIMEOUT);
        console.log("Button press detected — test passed.");
      } finally {
        await closePort(port);
      }
    },
    BUILD_TIMEOUT + READY_TIMEOUT + HUMAN_TIMEOUT + 60_000
  );
});
