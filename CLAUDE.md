# CLAUDE.md

## What is wasm-os?

Standalone ESP32 firmware that runs WebAssembly modules via WAMR (WebAssembly Micro Runtime). No cloud connectivity. Push `.wasm` files over USB serial, device runs `/littlefs/main.wasm` on startup.

## Structure

npm-workspaces monorepo (root `package.json` is the version source and workspace root):

- `wasm-os-core/` - ESP-IDF firmware project (C, ESP-IDF v5.5)
- `wasm-os-core/main/` - the firmware component
- `wasm-os-core/main/bindings/` - WASM-facing host API (one module per file, `.wit` docs alongside)
- `wasm-os-core/profiles/` - Build profiles for different ESP32 variants
- `mklfs/` - standalone npm package `@wasm-os/mklfs` (CLI command `mklfs`): littlefs images built in pure JS (littlefs compiled to an import-free WASM module); its littlefs version must match wasm-os-core's esp_littlefs — bump together
- `wasm-os-sdk/` - isomorphic JS SDK (npm package `wasm-os-sdk`): the serial protocol (pure Uint8Array, browser-safe), a transport-agnostic device client, Node (node-serialport) + browser (Web Serial) transports, and ESP-IDF partition-table parsing; unit tests in `wasm-os-sdk/tests/`
- `wasm-os-cli/` - Node.js CLI (npm package `wasm-os`), a thin commander wrapper over wasm-os-sdk plus esptool-js flashing
- `wasm-os-tests/` - hardware/e2e suites spanning firmware + CLI, with the WASM fixtures and toolchain fetch scripts

## Build & Flash

Requires ESP-IDF. Initialize with `get_idf` or `. $HOME/.espressif/v5.5.4/esp-idf/export.sh`.

```bash
cd wasm-os-core
idf.py @profiles/esp32s3-16mb-psram-oct set-target esp32s3 build
idf.py -p /dev/cu.usbmodem1101 flash
idf.py monitor
```

## CLI

```bash
npm install                        # once, at the repo root (npm workspaces)
cd wasm-os-cli
npx wasm-os push ./app.wasm        # push and run
npx wasm-os delete probe.bin       # remove files from the device (alias: rm)
npx wasm-os restart                # restart current app
npx wasm-os monitor                # serial monitor
npx wasm-os ports                  # list serial ports
npx wasm-os info                   # detect chip type and flash size
npx wasm-os flash ./firmware.bin   # flash firmware (no ESP-IDF needed)
npx wasm-os flash merged.bin --app ./app.wasm --env ./.env  # provision: firmware + app + settings in one flash
npx wasm-os push ./.env            # device settings (see below)
```

### Device settings (`/littlefs/.env`)

There is no NVS. All device configuration is a `.env` file on littlefs, pushed
like any other file. `KEY=VALUE` per line; `#` comments and blank lines are
ignored, values may be quoted:

```
WIFI_SSID=my-network
WIFI_PASS=secret
LOG_LEVEL=3
API_BASE=https://example.com
```

`LOG_LEVEL` is consumed by the firmware (`wasm-os-core/main/device_config.c`);
**every other key — including `WIFI_SSID`/`WIFI_PASS` — is passed to the WASM
app as an environment variable**. The firmware never connects to WiFi on its
own: the app reads the credentials via the `env` binding (`getenv`) and passes
them to `wifi_connect` explicitly. Restart the device for changes to take
effect — the file is read once at boot.

### Flashing without ESP-IDF

`wasm-os flash` writes a merged firmware image using `esptool-js` over
node-serialport, so end users never need the ESP-IDF toolchain. Produce the
image from a build with:

```bash
cd wasm-os-core/build/<profile> && esptool.py --chip <target> merge_bin -o merged.bin \
  --flash_mode dio --flash_size <size> --flash_freq 80m \
  <offsets from flasher_args.json>
```

A merged image is contiguous from `0x0`, so `merge_bin` pads the gaps between
partitions with `0xFF`. It ends below `littlefs`, so both the pushed `.wasm`
app and the `.env` config survive a firmware flash. `--erase` wipes littlefs
too, which does destroy them.

`--app`/`--env` bundle a littlefs partition into the same flash: the CLI
builds the filesystem image in-process (the `@wasm-os/mklfs` package — littlefs
compiled to WASM, pinned to the firmware's lfs version and geometry —
bump them together), locates the partition by parsing the partition table at
`0x8000` inside the image, and writes the whole partition image — esptool
erases exactly what it writes, and the full region must be erased or stale
metadata pairs from a previous filesystem could out-revision fresh ones
(the 0xFF bulk compresses to almost nothing on the wire). The device boots
with the app and settings already in place — no serial push needed.

Three things about `esptool-js` are load-bearing (see `wasm-os-cli/src/webserial.js`):
its published `lib/` uses extensionless imports Node's ESM resolver rejects, so
`flash.js` imports `esptool-js/bundle.js`; `Writable.toWeb()` never drains
serialport's buffer, so writes must be flushed explicitly or every command
times out; and `SerialPort.list()` reports `/dev/tty.*` while the CLI uses
`/dev/cu.*`, so paths are matched on their suffix or the USB product ID is
lost and the wrong reset sequence is chosen.

## Tests

All runnable from the repo root; `npm test` never touches hardware.

```bash
npm test                 # unit tests across workspaces (mklfs + wasm-os-sdk, no hardware)
npm run test:hw          # hardware integration (requires connected ESP32)
npm run test:wifi        # hardware: app connects to WiFi via the wifi binding and makes an HTTP request (needs wasm-os-tests/.env credentials)
npm run test:supervisor  # hardware: two-slot runtime — a supervisor app starts/stops/reclaims child apps
npm run test:touch       # interactive: human presses an on-screen button (HW-458/CYD board)
npm run test:touch:p4    # interactive: same, with LVGL on the JC8012P4A1C (ESP32-P4, 10.1" MIPI-DSI)
```

The hardware suites live in `wasm-os-tests/` (shared flashing/push/marker plumbing in `wasm-os-tests/lib.js`).

The hardware suite builds firmware, flashes it, compiles the AssemblyScript fixture to .wasm, pushes it via the serial protocol, and verifies execution. Match the profile to the connected board with `WASM_OS_PROFILE=esp32-4mb` (default is esp32s3).

The touch test pushes a plain-C app (compiled with wasi-sdk, auto-downloaded by `wasm-os-tests/scripts/fetch-wasi-sdk.sh`) that drives the CYD's ILI9341 display and XPT2046 touch entirely through the spi_master/gpio bindings. Note: LVGL-sized modules (~100 KB+) load but cannot instantiate on the PSRAM-less CYD — the module bytecode and the 64 KB linear-memory page cannot both fit its fragmented DRAM regions.

The P4 touch test (`test:touch:p4`, `WASM_OS_PROFILE=esp32p4-16mb-psram`) targets the JC8012P4A1C board and does use LVGL (v9, fetched by `wasm-os-tests/scripts/fetch-lvgl.sh`, compiled to wasm alongside the fixture): the P4's PSRAM removes the module-size limit. Its fixture drives the JD9365 800x1280 MIPI-DSI panel through the esp_lcd bindings and the GSL3680 touch controller (vendor firmware upload + Silead point algorithm, copied from the vendor demo) through the i2c_master bindings.

## Releases

Changesets manages the publishable npm packages (`@wasm-os/mklfs`, `wasm-os-sdk`, `wasm-os`); `wasm-os-tests` is private/ignored and pins workspace siblings with `*` ranges. Land every release-worthy change with `npx changeset`; `npm run version-packages` bumps versions and cascades internal dependency ranges (important on 0.x, where `^0.1.0` excludes `0.2.0`); `npm run release` tests, publishes in dependency order, and git-tags. `.github/workflows/release.yml` automates this via a "Version Packages" PR once the repo is on GitHub (needs an `NPM_TOKEN` secret). The firmware version in the root `package.json` is a separate axis, managed by hand. A serial-protocol change is at least a minor `wasm-os-sdk` bump, with the minimum firmware noted in the changelog.

## Serial Protocol

Binary protocol over USB serial with magic bytes `WOS!` (0x57 0x4F 0x53 0x21). Frame format: `[MAGIC:4] [CMD:1] [LEN:4 LE] [PAYLOAD:LEN]`. Commands: PUSH_BEGIN, PUSH_DATA (1KB chunks), PUSH_END, RESTART, DELETE (payload = filename). Device responds with ACK/NAK. Implemented in `wasm-os-core/main/serial_cmd.c` and `wasm-os-sdk/src/protocol.js` — keep the two in sync (same repo, same PR).

PUSH_BEGIN stops both app slots (child first, then main), because a busy guest in either slot starves the serial task and stalls the transfer. The main app restarts once the transfer ends — on success, failure, or when a later PUSH_BEGIN supersedes an abandoned one — so a push never leaves the device app-less; re-launching the child is the main app's job. Pushing any file therefore restarts the app; `main.wasm` additionally starts even if nothing was running.

## Architecture Conventions

- **App lifecycle** (`wasm-os-core/main/app_runtime.c`): two slots — `WOS_SLOT_MAIN` boots `/littlefs/main.wasm`; `WOS_SLOT_CHILD` is started only through the `app` binding, by the main app (the supervisor pattern). The WAMR runtime is initialized once at boot and never destroyed. Per slot, the slot's task is the sole owner of that slot's WAMR objects: never free runtime state from another task; request a stop via `app_runtime_stop(slot, timeout)`, which terminates guest execution and waits for the slot's own teardown.
- **Resource ownership** (`wasm-os-core/main/bindings/owner.c`): every guest resource is stamped with the creating slot, derived from the calling task. Teardown reclaims only the dying slot's handles/tasks/callbacks/regions. The `app` management binding is callable from the main slot only; the main app outranks the child (`priorities.h`), so a spinning child can always be stopped.
- **Handles** (`wasm-os-core/main/bindings/handle.h`): guests never see native pointers. Every host resource is registered in the typed, generation-checked handle table and validated with `wos_handle_deref` on each use. Leaked resources are reclaimed at app teardown.
- **Guest memory** (`wasm-os-core/main/bindings/common.h`): every guest buffer must go through `wos_guest_ptr(exec_env, addr, len)` — it bounds-checks the whole range. Never call `wasm_runtime_addr_app_to_native` directly in bindings.
- **Errors**: bindings return 0 on success, negative on failure. `WOS_ERR_*` codes (-1..-7) for binding-layer failures; ESP-IDF errors pass through negated. Constructors return a handle or 0. Documented per module in `wasm-os-core/main/bindings/*.wit` and `wasm-os-core/main/bindings/README.md`.
- **New bindings**: add `wasm-os-core/main/bindings/<name>.c` with a `wos_register_<name>` function, declare it in `modules.h`, add it to the registrar table in `bindings.c` and to `wasm-os-core/main/CMakeLists.txt`, and document the interface in a `.wit` file.
- **Threading**: WAMR exec envs are not thread-safe. Guest code on other FreeRTOS tasks must use a spawned exec env (see `wasm-os-core/main/bindings/task.c`).

## Key Files

- `wasm-os-core/main/main.c` - Boot orchestration only (filesystem, NVS, config, serial handler, app start)
- `wasm-os-core/main/wifi.c` - Station-mode WiFi state machine, driven by the `wifi` binding (the firmware never connects on its own)
- `wasm-os-core/main/app_runtime.c` - WASM runtime init and race-free app start/stop
- `wasm-os-core/main/device_config.c` - NVS-backed config (WiFi creds, env vars, log level)
- `wasm-os-core/main/serial_cmd.c` - USB serial protocol handler
- `wasm-os-core/main/bindings/app.c` - Child-app lifecycle binding (start/stop/status, main-slot only)
- `wasm-os-core/main/bindings/` - Hardware bindings (GPIO, SPI, I2S, LCD, HTTP, WebSocket, sockets, fs, ...)
- `wasm-os-sdk/src/protocol.js` - JS-side protocol implementation (shared by CLI, tests, and the browser)
- `wasm-os-sdk/src/client.js` - transport-agnostic device client (push/delete/restart, hard reset)
- `wasm-os-cli/src/device.js` - Port resolution and open/run/close scaffolding for commands
