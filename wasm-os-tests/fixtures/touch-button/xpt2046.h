/** XPT2046 resistive touch driver for the CYD (HW-458) board. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Initialize the touch controller's SPI bus and IRQ pin. */
void xpt2046_init(void);

/**
 * Read the current touch point in screen coordinates (240x320 portrait).
 * Returns true while the panel is being touched.
 */
bool xpt2046_read(int16_t* x, int16_t* y);
