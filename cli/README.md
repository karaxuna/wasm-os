# wasm-os CLI

Command-line tool for pushing WebAssembly modules to ESP32 devices running the wasm-os firmware, over USB serial.

## Install

```bash
npm install
npm link   # optional: makes the `wasm-os` command available globally
```

Without `npm link`, replace `wasm-os` with `npx wasm-os` (or `node src/index.js`) below.

## Commands

### `wasm-os push <file>`

Push a file to the device's LittleFS filesystem. When the destination is `main.wasm` (the default for a file named that, or via `--name main.wasm`), the running app is stopped first and the new one starts as soon as the transfer completes.

```bash
wasm-os push ./app.wasm                    # becomes /littlefs/app.wasm
wasm-os push ./build/out.wasm -n main.wasm # becomes /littlefs/main.wasm and runs
```

| Option | Description |
|---|---|
| `-n, --name <name>` | Destination filename on the device (defaults to the source filename) |
| `-p, --port <path>` | Serial port path (auto-detected if omitted) |
| `-b, --baud <rate>` | Baud rate (default 115200) |

Transfers are streamed in 1 KB chunks, each acknowledged by the device, and written to a temp file that atomically replaces the target — an interrupted push never corrupts the installed app.

### `wasm-os restart`

Stop and restart the current WASM app (`/littlefs/main.wasm`).

```bash
wasm-os restart
```

Options: `-p, --port`, `-b, --baud` as above.

### `wasm-os monitor`

Stream the device's serial output (logs from the firmware and the WASM app). Exit with Ctrl+C.

```bash
wasm-os monitor
```

Options: `-p, --port`, `-b, --baud` as above.

### `wasm-os ports`

List available serial ports with manufacturer and USB VID/PID, to help pick a `--port` value.

```bash
wasm-os ports
```

## Port auto-detection

When `--port` is omitted, the CLI scans for common ESP32 USB-serial bridges (Espressif USB-JTAG, CP210x, FTDI, CH340), preferring Espressif's own USB-JTAG interface. On macOS, `/dev/cu.*` is preferred over `/dev/tty.*`. Boards with auto-reset wiring (CH340/CP210x) reboot when the port opens; commands retry their opening frame until the device answers.

## Tests

```bash
npm test           # protocol unit tests — no hardware needed
npm run test:hw    # hardware integration — flashes and exercises a connected ESP32
npm run test:touch # interactive — a human must press a button on the touchscreen (HW-458/CYD board)
```

Environment variables for the hardware suites:

| Variable | Description |
|---|---|
| `WASM_OS_PROFILE` | Build profile matching the connected board (default `esp32s3-16mb-psram-oct` for `test:hw`, `esp32-4mb` for `test:touch`) |
| `WASM_OS_SKIP_FLASH` | Set to `1` to skip the firmware build+flash step (`test:touch`) |
| `IDF_PATH` | ESP-IDF location (defaults to the v5.5.4 install) |
| `WASI_SDK_PATH` | Use an existing wasi-sdk instead of auto-downloading one |

## Layout

- `src/index.js` — command definitions (commander)
- `src/protocol.js` — binary frame protocol, shared with `main/serial_cmd.c` in the firmware (keep in sync)
- `src/serial.js` — port handling, response parsing, retries
- `src/device.js` — port resolution and open/run/close scaffolding used by commands
- `tests/fixtures/` — WASM test apps (AssemblyScript blinky; plain-C touch-button app for the CYD)
- `scripts/fetch-wasi-sdk.sh` — downloads the pinned C→WASM toolchain into `toolchain/` (gitignored) on first use

## Protocol

Binary frames over serial, magic `WOS!` (0x57 0x4F 0x53 0x21):

```
[MAGIC:4] [CMD:1] [LEN:4 LE] [PAYLOAD:LEN]
```

Commands: `PUSH_BEGIN` (0x01, payload = total size + filename), `PUSH_DATA` (0x02, 1 KB chunks), `PUSH_END` (0x03), `RESTART` (0x04). The device answers each frame with `ACK` (0x80) or `NAK` (0x81, payload = error message).
