// GPIO bindings to wasm-os host functions.
// All calls return 0 on success, negative on error (see main/bindings/README.md).

// @ts-ignore
@external("gpio", "gpio_set_level")
export declare function gpioSetLevel(gpioNum: u32, level: u32): i32;

// @ts-ignore
@external("gpio", "gpio_set_direction")
export declare function gpioSetDirection(gpioNum: u32, mode: u32): i32;

// GPIO modes
export const GPIO_MODE_OUTPUT: u32 = 2;
