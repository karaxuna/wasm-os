#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "wasm_export.h"

/**
 * Unified error space returned to WASM guests by every wasm-os binding.
 *
 * 0 means success, negative means failure. Binding-layer failures use the
 * small reserved codes below; ESP-IDF errors pass through negated (e.g.
 * ESP_ERR_NO_MEM 0x101 becomes -0x101, well outside the reserved range) so
 * guests can distinguish the two.
 */
#define WOS_OK 0
#define WOS_ERR_INVALID_HANDLE (-1) /* stale, forged, or wrong-type handle */
#define WOS_ERR_BAD_MEMORY (-2)     /* guest pointer/length out of bounds */
#define WOS_ERR_NO_MEM (-3)         /* host allocation failed */
#define WOS_ERR_INTERNAL (-4)       /* unspecified host-side failure */
#define WOS_ERR_NOT_FOUND (-5)      /* named resource does not exist */
#define WOS_ERR_INVALID_ARG (-6)    /* argument rejected before reaching hardware */
#define WOS_ERR_UNSUPPORTED (-7)    /* not available on this chip target */

/** Map an esp_err_t into the guest error space (ESP_OK -> WOS_OK). */
int32_t wos_err(esp_err_t err);

/**
 * Validate the guest buffer [addr, addr+len) against the module's linear
 * memory and convert it to a native pointer. Returns NULL when addr is 0,
 * the range is out of bounds, or there is no module instance.
 *
 * Every binding that receives a raw guest address MUST go through this;
 * wasm_runtime_addr_app_to_native alone does not check the length.
 */
void* wos_guest_ptr(wasm_exec_env_t exec_env, uint64_t addr, uint64_t len);

/** Write a u32 out-parameter into guest memory. False when out of bounds. */
bool wos_guest_write_u32(wasm_exec_env_t exec_env, uint64_t addr, uint32_t value);

/**
 * snprintf-style copy-out: writes src (plus NUL terminator) into the guest
 * buffer when cap is large enough, and always returns the length of src.
 * Returns WOS_ERR_BAD_MEMORY if the buffer address is invalid.
 */
int32_t wos_guest_copy_out(wasm_exec_env_t exec_env, uint64_t buf_addr, uint32_t buf_cap, const void* src,
                           uint32_t src_len);
