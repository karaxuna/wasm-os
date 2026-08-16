# wasm-os SDK

Isomorphic JavaScript SDK for talking to ESP32 devices running the wasm-os
firmware: the binary serial protocol, a transport-agnostic device client, and
transports for Node (node-serialport) and the browser (Web Serial API).

The protocol layer is pure `Uint8Array`/`DataView` — no Node `Buffer` — so the
same code runs unbundled in the browser. `serialport` is an optional peer
dependency, required lazily only by the Node transport.

## Node

```js
const { autoDetectPort, openNodeSerialTransport, createDeviceClient } = require("wasm-os-sdk");

const portPath = await autoDetectPort();
const transport = await openNodeSerialTransport(portPath);
const client = createDeviceClient(transport);

await client.pushFile(wasmBytes, "main.wasm", {
  onProgress: (sent, total) => console.log(`${sent}/${total}`),
});
await transport.close();
```

## Browser (Chromium-only Web Serial)

```js
import { openWebSerialTransport, createDeviceClient } from "wasm-os-sdk";

const serialPort = await navigator.serial.requestPort();
const transport = await openWebSerialTransport(serialPort);
const client = createDeviceClient(transport);

await client.pushFile(wasmBytes, "main.wasm");
await transport.close();
```

## API

- `protocol.js` — frame building/parsing, shared byte-for-byte with
  `wasm-os-core/main/serial_cmd.c` (keep the two in sync)
- `createDeviceClient(transport)` — `pushFile(data, name, {onProgress, beginAttempts})`,
  `deleteFile(name)`, `restart()`, plus raw `sendFrame`/`sendFrameWithRetry`
- `hardReset(transport)` — reboot into the application (device settings are
  read once at boot)
- Transports implement `{ write, subscribe, setSignals, close }`; anything
  matching that shape works with the client.
- `parsePartitionTable(image)` / `findPartition(image, label)` — read the
  ESP-IDF partition table out of a merged firmware image

littlefs image building lives in the sibling [`@wasm-os/mklfs`](../mklfs) package.

## Tests

```bash
npm test   # protocol unit tests, no hardware
```
