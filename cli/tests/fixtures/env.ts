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
@external("env", "reboot")
export declare function reboot(): void;
