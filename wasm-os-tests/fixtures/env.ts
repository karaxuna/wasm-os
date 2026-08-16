// Bindings to wasm-os host functions

// @ts-ignore
@external("env", "print")
export declare function print(message: ArrayBuffer): void;

// @ts-ignore
@external("env", "println")
export declare function println(message: ArrayBuffer): void;

// @ts-ignore
@external("env", "delay")
export declare function delay(milliseconds: u32): void;

// @ts-ignore
@external("env", "millis")
export declare function millis(): u32;

// @ts-ignore
@external("env", "reboot")
export declare function reboot(): void;

// snprintf-style: returns the value length and copies value + NUL into buf
// when cap is large enough; -5 when the variable is not set.
// @ts-ignore
@external("env", "getenv")
export declare function getenv(name: ArrayBuffer, buf: u32, cap: u32): i32;
