// WiFi bindings to wasm-os host functions.
// All calls return 0 on success, negative on error (see main/bindings/README.md).

// @ts-ignore
@external("wifi", "wifi_connect")
export declare function wifiConnect(ssid: ArrayBuffer, pass: ArrayBuffer): i32;

// @ts-ignore
@external("wifi", "wifi_disconnect")
export declare function wifiDisconnect(): i32;

// @ts-ignore
@external("wifi", "wifi_state")
export declare function wifiState(): i32;

// @ts-ignore
@external("wifi", "wifi_wait")
export declare function wifiWait(timeoutMs: i32): i32;

// @ts-ignore
@external("wifi", "wifi_ip")
export declare function wifiIp(buf: u32, cap: u32): i32;

// WiFi states
export const WIFI_STATE_DISCONNECTED: i32 = 0;
export const WIFI_STATE_CONNECTING: i32 = 1;
export const WIFI_STATE_CONNECTED: i32 = 2;
export const WIFI_STATE_FAILED: i32 = 3;
