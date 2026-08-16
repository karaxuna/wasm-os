/**
 * Interactive LVGL touch test for the JC8012P4A1C board (ESP32-P4 with
 * 32 MB PSRAM, JD9365 800x1280 10.1" MIPI-DSI panel, GSL3680 I2C touch).
 *
 * Flashes wasm-os, pushes an LVGL app (plain C + LVGL compiled with
 * wasi-sdk) that draws a large green button, then waits for a HUMAN to
 * press it on the screen. Not part of `npm test` — run:
 *
 *   npm run test:touch:p4
 *
 * Device settings come from cli/tests/.env (see .env.example) — the app needs
 * WiFi to fetch its buttons over HTTP.
 *
 * Env:
 *   WASM_OS_PROFILE     build profile (default esp32p4-16mb-psram)
 *   WASM_OS_SKIP_FLASH  set to 1 when the firmware is already flashed
 */
const { execSync } = require("child_process");
const path = require("path");
const fs = require("fs");
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
const FIXTURE_DIR = path.resolve(__dirname, "fixtures/lvgl-touch-p4");
const APP_WASM = path.join(FIXTURE_DIR, "app.wasm");
const ENV_FILE = path.resolve(__dirname, ".env");

const IDF_PATH = process.env.IDF_PATH || "/Users/kakhaber/.espressif/v5.5.4/esp-idf";
const PROFILE = process.env.WASM_OS_PROFILE || "esp32p4-16mb-psram";
const SKIP_FLASH = process.env.WASM_OS_SKIP_FLASH === "1";

const FLASH_TIMEOUT = 300_000;
const BUILD_TIMEOUT = 900_000; // first run downloads wasi-sdk and compiles LVGL
const READY_TIMEOUT = 120_000; // LVGL renders 800x1280 through the interpreter
const HUMAN_TIMEOUT = 60_000;
const WIFI_TIMEOUT = 45_000; // wifi_connect gives up after 30s

let portPath;

/** Resolve when `marker` shows up in the serial stream; reject on timeout. */
function waitForMarker(port, marker, timeoutMs) {
  return new Promise((resolve, reject) => {
    let buf = "";
    const timer = setTimeout(() => {
      port.off("data", onData);
      reject(new Error(`Timed out after ${timeoutMs / 1000}s waiting for "${marker}". Device output:\n${buf}`));
    }, timeoutMs);

    const onData = (data) => {
      buf += data.toString();
      if (buf.includes(marker)) {
        clearTimeout(timer);
        port.off("data", onData);
        resolve(buf);
      }
    };

    port.on("data", onData);
  });
}

/** Stream a file to /littlefs/<name> over the serial protocol. */
async function pushFile(port, data, name) {
  await sendCommandWithRetry(port, buildPushBeginFrame(data.length, name), {
    timeout: PUSH_BEGIN_TIMEOUT,
  });
  for (let i = 0; i < data.length; i += CHUNK_SIZE) {
    await sendAndWaitForResponse(port, buildPushDataFrame(data.subarray(i, Math.min(i + CHUNK_SIZE, data.length))));
  }
  await sendAndWaitForResponse(port, buildPushEndFrame());
}

/**
 * Pulse EN via RTS while leaving IO0 (DTR) high, so the chip reboots into the
 * application rather than the ROM downloader. Device settings are only read at
 * boot, so pushing .env is pointless without this.
 */
function hardReset(port) {
  return new Promise((resolve) => {
    port.set({ dtr: false, rts: true }, () => {
      setTimeout(() => {
        port.set({ dtr: false, rts: false }, () => {
          return resolve();
        });
      }, 150);
    });
  });
}

beforeAll(async () => {
  portPath = await autoDetectPort();
  if (!portPath) {
    throw new Error("No ESP32 device detected. Connect the JC8012P4A1C board via USB.");
  }
  console.log(`Using serial port: ${portPath}`);
});

describe("lvgl touch button on esp32p4 (human in the loop)", () => {
  it(
    "flashes wasm-os",
    () => {
      if (SKIP_FLASH) {
        console.log("WASM_OS_SKIP_FLASH=1, skipping firmware flash");
        return;
      }
      execSync(
        `bash -c '. ${IDF_PATH}/export.sh 2>/dev/null && cd ${FIRMWARE_DIR} && idf.py @profiles/${PROFILE} -p ${portPath} build flash'`,
        { stdio: "inherit", timeout: FLASH_TIMEOUT }
      );
    },
    FLASH_TIMEOUT
  );

  it(
    "builds the LVGL app, pushes it, and waits for a human button press",
    async () => {
      console.log("Building lvgl-touch-p4 fixture (first run downloads wasi-sdk + LVGL)...");
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
        const hasEnv = fs.existsSync(ENV_FILE);
        if (hasEnv) {
          console.log("Pushing device settings (.env)...");
          await pushFile(port, fs.readFileSync(ENV_FILE), ".env");
        } else {
          console.log(`No ${ENV_FILE}; the app cannot fetch buttons without WiFi.`);
        }

        console.log("Pushing app...");
        await pushFile(port, wasm, "main.wasm");

        /* Push before rebooting, not after: settings are only read at boot,
         * and a freshly booted app blocks the serial task through its first
         * full-screen render, which starves an in-flight transfer. */
        console.log("Rebooting...");
        await hardReset(port);
        if (hasEnv) {
          await waitForMarker(port, "Got IP:", WIFI_TIMEOUT);
          console.log("Device online.");
        }

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
    BUILD_TIMEOUT + WIFI_TIMEOUT + READY_TIMEOUT + HUMAN_TIMEOUT + 60_000
  );
});
