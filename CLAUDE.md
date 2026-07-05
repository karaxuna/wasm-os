# CLAUDE.md

## What is wasm-os?

Standalone ESP32 firmware that runs WebAssembly modules via WAMR (WebAssembly Micro Runtime). No cloud connectivity. Push `.wasm` files over USB serial, device runs `/littlefs/main.wasm` on startup.

## Structure

- `main/` - ESP-IDF firmware (C, ESP-IDF v5.5)
- `main/bindings/` - WASM-facing host API (one module per file, `.wit` docs alongside)
- `cli/` - Node.js CLI (`wasm-os`) for pushing .wasm files to device over USB serial
- `profiles/` - Build profiles for different ESP32 variants

## Build & Flash

Requires ESP-IDF. Initialize with `get_idf` or `. $HOME/.espressif/v5.5.4/esp-idf/export.sh`.

```bash
idf.py @profiles/esp32s3-16mb-psram-oct set-target esp32s3 build
idf.py -p /dev/cu.usbmodem1101 flash
idf.py monitor
```

## CLI

```bash
cd cli && npm install
npx wasm-os push ./app.wasm        # push and run
npx wasm-os restart                # restart current app
npx wasm-os monitor                # serial monitor
npx wasm-os ports                  # list serial ports
```

## Tests

```bash
cd cli && npm test        # protocol unit tests (no hardware)
cd cli && npm run test:hw # hardware integration (requires connected ESP32)
```

The hardware suite builds firmware, flashes it, compiles the AssemblyScript fixture to .wasm, pushes it via the serial protocol, and verifies execution.

## Serial Protocol

Binary protocol over USB serial with magic bytes `WOS!` (0x57 0x4F 0x53 0x21). Frame format: `[MAGIC:4] [CMD:1] [LEN:4 LE] [PAYLOAD:LEN]`. Commands: PUSH_BEGIN, PUSH_DATA (1KB chunks), PUSH_END, RESTART. Device responds with ACK/NAK. Implemented in `main/serial_cmd.c` and `cli/src/protocol.js` — keep the two in sync.

## Architecture Conventions

- **App lifecycle** (`main/app_runtime.c`): the app task is the sole owner of all WAMR objects. Never free runtime state from another task; request a stop via `app_runtime_stop()`, which terminates guest execution and waits for the app task's own teardown.
- **Handles** (`main/bindings/handle.h`): guests never see native pointers. Every host resource is registered in the typed, generation-checked handle table and validated with `wos_handle_deref` on each use. Leaked resources are reclaimed at app teardown.
- **Guest memory** (`main/bindings/common.h`): every guest buffer must go through `wos_guest_ptr(exec_env, addr, len)` — it bounds-checks the whole range. Never call `wasm_runtime_addr_app_to_native` directly in bindings.
- **Errors**: bindings return 0 on success, negative on failure. `WOS_ERR_*` codes (-1..-7) for binding-layer failures; ESP-IDF errors pass through negated. Constructors return a handle or 0. Documented per module in `main/bindings/*.wit` and `main/bindings/README.md`.
- **New bindings**: add `main/bindings/<name>.c` with a `wos_register_<name>` function, declare it in `modules.h`, add it to the registrar table in `bindings.c` and to `main/CMakeLists.txt`, and document the interface in a `.wit` file.
- **Threading**: WAMR exec envs are not thread-safe. Guest code on other FreeRTOS tasks must use a spawned exec env (see `main/bindings/task.c`).

## Key Files

- `main/main.c` - Boot orchestration only (filesystem, NVS, config, WiFi, serial handler, app start)
- `main/app_runtime.c` - WASM runtime init and race-free app start/stop
- `main/device_config.c` - NVS-backed config (WiFi creds, env vars, log level)
- `main/serial_cmd.c` - USB serial protocol handler
- `main/bindings/` - Hardware bindings (GPIO, SPI, I2S, LCD, HTTP, WebSocket, sockets, storage, fs, ...)
- `cli/src/protocol.js` - CLI-side protocol implementation
- `cli/src/device.js` - Port resolution and open/run/close scaffolding for commands
