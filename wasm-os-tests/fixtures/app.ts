// Child-app lifecycle bindings (main slot only; -7 from the child).
// All calls return 0 on success, negative on error (see wasm-os-core/main/bindings/README.md).

// @ts-ignore
@external("app", "app_start")
export declare function appStart(name: ArrayBuffer): i32;

// @ts-ignore
@external("app", "app_stop")
export declare function appStop(timeoutMs: i32): i32;

// @ts-ignore
@external("app", "app_status")
export declare function appStatus(): i32;

// @ts-ignore
@external("app", "app_last_error")
export declare function appLastError(buf: u32, cap: u32): i32;

// @ts-ignore
@external("app", "app_reset_reason")
export declare function appResetReason(): i32;

// Child states
export const APP_STATE_STOPPED: i32 = 0;
export const APP_STATE_STARTING: i32 = 1;
export const APP_STATE_RUNNING: i32 = 2;
export const APP_STATE_STOPPING: i32 = 3;
export const APP_STATE_CRASHED: i32 = 4;
