#pragma once

#include <stdbool.h>

#include "owner.h"

/**
 * Public interface of the bindings layer.
 *
 * The firmware registers all native symbols once at boot, and resets one
 * slot's binding state (handle table entries, callbacks, shared memory
 * exports, spawned tasks) whenever that slot is torn down. The other slot's
 * state is untouched.
 */

/** Register every binding module's native symbols with WAMR. */
bool wos_register_all_bindings(void);

/** Release one slot's binding state. Safe to call when nothing is live. */
void wos_bindings_reset_slot(wos_slot_t slot);

/**
 * Number of the slot's guest-spawned FreeRTOS tasks still executing. Slot
 * teardown must wait for this to reach zero before deinstantiating.
 */
int wos_bindings_active_tasks(wos_slot_t slot);
