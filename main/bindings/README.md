# wasm-os host bindings

Native functions exposed to WASM guests, one module per file. The `.wit`
files document each module's guest-facing interface; nothing consumes them at
build time.

## ABI conventions

Every module follows the same rules:

**Handles.** Host resources (driver handles, config structs, sockets, open
files) are referred to by opaque `u32` handles backed by a typed, generation-
checked table (`handle.h`). Guests never see native pointers, and a stale,
forged, or wrong-type handle is rejected — never dereferenced.

- *Constructors* (`*_create`, `*_connect`, `*_open`, `*_init`) return a
  handle, or `0` on failure.
- Functions that produce a handle alongside a status write it through a `u32`
  out-pointer in guest memory and return an error code.

**Error codes.** Functions that return a status use `0` for success and
negative values for failure:

| code | meaning |
|------|---------|
| `-1` | invalid handle |
| `-2` | guest pointer/length out of bounds |
| `-3` | host out of memory |
| `-4` | internal/unspecified failure |
| `-5` | not found |
| `-6` | invalid argument |
| `-7` | unsupported on this chip target |
| `<= -0x100` | negated ESP-IDF error code (e.g. `-0x101` = `ESP_ERR_NO_MEM`) |

Functions that return a count (bytes read/written) return the non-negative
count or a negative error from the same table.

**Buffers.** Guest buffers are always passed as `(address, length)` and the
full range is bounds-checked before use. Variable-sized getters are
snprintf-style: they return the full size of the value and copy it out only
when the provided buffer is large enough.

**Cleanup.** Anything the guest leaks is released automatically when the app
stops; explicit `*_destroy`/`*_close` calls free resources early.
