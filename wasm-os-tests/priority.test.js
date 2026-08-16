/**
 * Regression test for the task-priority invariant in
 * wasm-os-core/main/priorities.h.
 *
 * Pushes an app that spins forever without ever yielding, then requires the
 * device to still answer the CLI. The static assertions in priorities.h guard
 * the numbers; this guards the behaviour they exist for.
 *
 * !! CURRENTLY FAILS, AND WEDGES THE BOARD !!
 *
 * Raising the serial handler above the app was necessary but is not
 * sufficient. app_runtime_stop() cannot terminate a guest that never yields:
 * WAMR's terminate flag goes unnoticed, the stop burns APP_STOP_TIMEOUT_MS
 * (10s) and then force-deletes the task mid-runtime — after which the device
 * stops answering entirely and drops off USB, needing a physical replug and a
 * `wasm-os flash --erase` (littlefs must go, or the spinning app auto-starts
 * again). The same hazard predates the PUSH_BEGIN change: `restart` has always
 * called app_runtime_stop().
 *
 * Do not run this casually — it costs a replug. Assumes firmware is flashed:
 *
 *   npm run test:priority
 */
const { execSync } = require("child_process");
const path = require("path");
const fs = require("fs");
const { FIXTURES_DIR, openDevice, autoDetectPort } = require("./lib");

const HOSTILE_WASM = path.join(FIXTURES_DIR, "hostile.wasm");

const BUILD_TIMEOUT = 60_000;
const SPIN_SETTLE_MS = 3000;

let portPath;

beforeAll(async () => {
  portPath = await autoDetectPort();
  if (!portPath) {
    throw new Error("No ESP32 device detected. Connect a board via USB.");
  }
  console.log(`Using serial port: ${portPath}`);

  execSync(
    [
      "npx asc",
      path.join(FIXTURES_DIR, "hostile.ts"),
      "-o",
      HOSTILE_WASM,
      "--stackSize 3072",
      "--initialMemory 1",
      "--maximumMemory 1",
      "--noAssert",
      "--importMemory",
      "--exportRuntime",
    ].join(" "),
    { cwd: FIXTURES_DIR, stdio: "inherit", timeout: BUILD_TIMEOUT }
  );
}, BUILD_TIMEOUT + 30_000);

describe("serial handler outranks a spinning guest", () => {
  it("stays reachable while an app spins without yielding", async () => {
    const wasm = fs.readFileSync(HOSTILE_WASM);
    const { transport, client } = await openDevice(portPath);

    try {
      // PUSH_END starts it, so the device is spinning from here on.
      console.log("Pushing the spinning app as main.wasm...");
      await client.pushFile(wasm, "main.wasm", { beginAttempts: 1 });
      await new Promise((r) => setTimeout(r, SPIN_SETTLE_MS));

      // The real assertions: every command below has to get through while
      // the guest is monopolising the CPU.
      console.log("PUSH while spinning...");
      await client.pushFile(Buffer.from("reachable\n"), "priority-probe.txt", { beginAttempts: 1 });

      console.log("DELETE while spinning...");
      await client.deleteFile("priority-probe.txt");

      console.log("RESTART while spinning...");
      await client.restart();
      console.log("All commands answered.");
    } finally {
      // Leave no spinning app behind: delete it, then restart into nothing.
      try {
        await client.deleteFile("main.wasm");
        await client.restart();
      } catch (err) {
        console.warn(`Cleanup failed: ${err.message}`);
      }
      await transport.close();
    }
  }, 120_000);
});
