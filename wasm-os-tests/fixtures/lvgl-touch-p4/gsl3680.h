/** GSL3680 capacitive touch on the JC8012P4A1C board (I2C addr 0x40). */
#pragma once

#include <stdint.h>

/* Returns 0 on success, negative wasm-os error otherwise. */
int gsl3680_init(void);

/* Returns 1 with the first finger's panel coordinates, 0 when not touched. */
int gsl3680_read(int16_t* x, int16_t* y);
