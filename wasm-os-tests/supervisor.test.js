/**
 * Hardware test for the two-slot runtime and the "app" binding: pushes a
 * simulated supervisor as main.wasm plus two child fixtures, then verifies
 * from serial output that the supervisor can detect a crashing child,
 * reclaim it, stay scheduled above a spinning child, and force-stop it.
 *
 *   npm run test:supervisor
 *
 * Env:
 *   WASM_OS_PROFILE     build profile (default esp32s3-16mb-psram-oct)
 *   WASM_OS_SKIP_FLASH  set to 1 when the firmware is already flashed
 */
const { execSync } = require("child_process");
const path = require("path");
const fs = require("fs");
const { openPort, closePort, autoDetectPort } = require("wasm-os/src/serial");
const { FIXTURES_DIR, flashFirmware, pushFile, waitForMarker } = require("./lib");

const FIXTURES = ["supervisor-sim", "child-crash", "child-spin"];

const PROFILE = process.env.WASM_OS_PROFILE || "esp32s3-16mb-psram-oct";
const SKIP_FLASH = process.env.WASM_OS_SKIP_FLASH === "1";

const FLASH_TIMEOUT = 300_000;
const RUN_TIMEOUT = 60_000;

let portPath;

function compiledPath(name) {
  return path.resolve(FIXTURES_DIR, `${name}.wasm`);
}

beforeAll(async () => {
  portPath = await autoDetectPort();
  if (!portPath) {
    throw new Error("No ESP32 device detected. Connect a board via USB.");
  }
  console.log(`Using serial port: ${portPath}`);
});

describe("two-slot runtime", () => {
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
    "supervisor manages, survives, and reclaims child apps",
    async () => {
      console.log("Compiling AssemblyScript fixtures...");
      for (const name of FIXTURES) {
        execSync(
          [
            "npx asc",
            path.resolve(FIXTURES_DIR, `${name}.ts`),
            "-o",
            compiledPath(name),
            "--stackSize 3072",
            "--initialMemory 1",
            "--maximumMemory 1",
            "--noAssert",
            "--importMemory",
            "--exportRuntime",
          ].join(" "),
          { cwd: FIXTURES_DIR, stdio: "inherit", timeout: 30_000 }
        );
      }

      // Give the device time to boot after flashing.
      await new Promise((r) => setTimeout(r, 5000));

      const port = await openPort(portPath);
      try {
        // Children first; the supervisor goes last as main.wasm so its
        // auto-start finds both files already in place.
        console.log("Pushing child apps...");
        await pushFile(port, fs.readFileSync(compiledPath("child-crash")), "child-crash.wasm");
        await pushFile(port, fs.readFileSync(compiledPath("child-spin")), "child-spin.wasm");

        console.log("Pushing supervisor as main.wasm...");
        await pushFile(port, fs.readFileSync(compiledPath("supervisor-sim")), "main.wasm");

        const output = await waitForMarker(port, "SUP_DONE", RUN_TIMEOUT);
        console.log("Device output:\n", output);

        // Crash detection and reclaim
        expect(output).toContain("CHILD_CRASH_RUNNING");
        expect(output).toContain("CHILD_POLICY rc=-7"); // child may not manage apps
        expect(output).toContain("CHILD_CRASH_DETECTED");
        expect(output).toContain("CHILD_ERROR");
        expect(output).toContain("CHILD_RECLAIMED");

        // Supervisor stays scheduled above a spinning child, then reclaims it.
        // rc=0 when WAMR's terminate flag interrupts the loop (the usual
        // case); a negative rc means the force-delete path had to reclaim.
        expect(output).toContain("CHILD_SPIN_RUNNING");
        expect(output).toContain("SUP_ALIVE 3");
        expect(output).toMatch(/SPIN_STOP rc=(0|-\d+) status=0/);
      } finally {
        await closePort(port);
      }
    },
    RUN_TIMEOUT + 60_000
  );
});

afterAll(() => {
  for (const name of FIXTURES) {
    if (fs.existsSync(compiledPath(name))) {
      fs.unlinkSync(compiledPath(name));
    }
  }
});
