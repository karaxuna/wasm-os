#pragma once

#include <stdbool.h>

/**
 * Public interface of the bindings layer.
 *
 * The firmware registers all native symbols once after the WASM runtime is
 * initialized, and resets per-app state (handle table, callbacks, shared
 * memory exports) whenever an app is torn down.
 */

/** Register every binding module's native symbols with WAMR. */
bool wos_register_all_bindings(void);

/** Release all per-app binding state. Safe to call when nothing is live. */
void wos_bindings_reset(void);

/**
 * Number of guest-spawned FreeRTOS tasks still executing. App teardown must
 * wait for this to reach zero before destroying the runtime.
 */
int wos_bindings_active_tasks(void);
