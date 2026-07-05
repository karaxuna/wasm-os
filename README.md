# wasm-os

Run WebAssembly modules on ESP32 microcontrollers. Push `.wasm` binaries to the device over USB serial; the firmware runs `/littlefs/main.wasm` in the WAMR runtime with sandboxed access to the hardware.

## Structure

- `main/` - ESP32 firmware (C, ESP-IDF v5.5, WAMR runtime)
  - `main/bindings/` - the WASM-facing host API, one module per file (see `main/bindings/README.md` for the ABI conventions)
- `cli/` - CLI tool (Node.js) for pushing WASM files to the device
- `profiles/` - build profiles per ESP32 variant / flash size

## Architecture

- **`app_runtime.c`** owns the app lifecycle. The app runs on its own FreeRTOS task, which is the sole owner of all WAMR objects; stopping interrupts guest execution via `wasm_runtime_terminate()` and waits for the app task to tear itself down.
- **`bindings/handle.c`** is a typed, generation-checked handle table. Guests refer to host resources (sockets, files, driver handles, configs) by opaque `u32` handles - never raw pointers - and anything the app leaks is released at teardown.
- **`bindings/common.c`** gives every binding the same guest-memory validation (`address+length` bounds checks on all buffers) and a unified error space.
- **`serial_cmd.c`** implements the framed USB-serial protocol used by the CLI.
- **`device_config.c`** loads WiFi credentials, environment variables, and log level from NVS at boot.

## Firmware

### Build & Flash

Requires [ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/get-started/).

```bash
# Initialize ESP-IDF
. $HOME/esp/esp-idf/export.sh

# Build with a profile (target + flash size + PSRAM in one)
idf.py @profiles/esp32s3-16mb-psram-oct set-target esp32s3 build

# Flash and monitor
idf.py @profiles/esp32s3-16mb-psram-oct -p /dev/cu.usbmodem1101 flash
idf.py monitor
```

### Supported Targets

ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6, ESP32-P4

## CLI

### Install

```bash
cd cli
npm install
npm link
```

### Usage

```bash
# Push a WASM binary to the device (main.wasm restarts the app)
wasm-os push ./app.wasm

# Restart the WASM module
wasm-os restart

# Monitor serial output
wasm-os monitor

# List available serial ports
wasm-os ports
```

### Options

- `-p, --port <path>` - Serial port (auto-detected if omitted)
- `-b, --baud <rate>` - Baud rate (default: 115200)

## Tests

```bash
cd cli
npm test          # protocol unit tests, no hardware needed
npm run test:hw   # full hardware integration suite (builds, flashes, pushes)
```

## How It Works

1. Firmware boots, mounts LittleFS, loads config from NVS, and connects WiFi when credentials are stored
2. `/littlefs/main.wasm` runs in WAMR on a dedicated task
3. A serial command handler listens for framed commands from the CLI (magic `WOS!`)
4. When a new `main.wasm` is pushed, the running app is stopped cleanly (guest execution is terminated, leaked resources are reclaimed), the file is written, and the new module starts

## Hardware Bindings

WASM modules access hardware through the modules documented in `main/bindings/*.wit`:

- GPIO (digital I/O)
- SPI master
- I2S (audio)
- MIPI-DSI LCD (ESP32-P4)
- HTTP client / WebSocket client / TCP sockets
- Persistent storage (NVS)
- Filesystem (sandboxed to LittleFS)
- Tasks, callbacks, shared memory, WASI stdout/stderr
