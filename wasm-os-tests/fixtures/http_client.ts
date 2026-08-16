// HTTP client bindings to wasm-os host functions.
// Constructors return a handle (0 = failure); other calls return 0 on
// success, negative on error (see wasm-os-core/main/bindings/README.md).

// @ts-ignore
@external("http_client", "http_client_config_create")
export declare function httpClientConfigCreate(): u32;

// @ts-ignore
@external("http_client", "http_client_config_set_url")
export declare function httpClientConfigSetUrl(config: u32, url: ArrayBuffer): i32;

// @ts-ignore
@external("http_client", "http_client_config_set_timeout_ms")
export declare function httpClientConfigSetTimeoutMs(config: u32, timeoutMs: i32): i32;

// @ts-ignore
@external("http_client", "http_client_config_destroy")
export declare function httpClientConfigDestroy(config: u32): i32;

// @ts-ignore
@external("http_client", "http_client_init")
export declare function httpClientInit(config: u32): u32;

// Runs the whole request; returns the HTTP status code or a negative error.
// @ts-ignore
@external("http_client", "http_client_perform")
export declare function httpClientPerform(client: u32): i32;

// @ts-ignore
@external("http_client", "http_client_cleanup")
export declare function httpClientCleanup(client: u32): i32;
