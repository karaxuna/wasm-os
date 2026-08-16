#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "owner.h"

/**
 * Typed handle table for host resources exposed to WASM guests.
 *
 * Guests never see native pointers: every host object (driver handle, config
 * struct, socket, open file, ...) is registered here and referred to by an
 * opaque 32-bit handle. Dereferencing validates the index, a generation
 * counter (catches use-after-destroy), and the expected type (catches a gpio
 * handle passed where an i2s channel was expected). A forged or stale handle
 * can therefore never reach a native pointer.
 *
 * The table also owns cleanup: every handle is stamped with the slot that
 * created it (wos_owner_current() at creation time), and when a slot is torn
 * down wos_handles_destroy_owned() releases everything that slot leaked —
 * and nothing the other slot still uses.
 */

typedef uint32_t wos_handle_t;

#define WOS_HANDLE_INVALID 0u

typedef struct {
  const char* name;           /* for diagnostics: what the handle refers to */
  void (*destroy)(void* ptr); /* releases the resource; NULL if not owned */
} wos_handle_type_t;

/** Prepare the table for a fresh app run. Idempotent. */
void wos_handles_init(void);

/**
 * Register a resource and return its handle, or WOS_HANDLE_INVALID when the
 * table is full. `ptr` must be non-NULL.
 */
wos_handle_t wos_handle_create(const wos_handle_type_t* type, void* ptr);

/**
 * Resolve a handle of the expected type to its native pointer.
 * Returns NULL (and logs the expected type) for invalid handles.
 */
void* wos_handle_deref(wos_handle_t handle, const wos_handle_type_t* type);

/**
 * Destroy the resource behind a handle: runs the type's destroy hook and
 * frees the slot. Returns WOS_OK or WOS_ERR_INVALID_HANDLE.
 */
int32_t wos_handle_destroy(wos_handle_t handle, const wos_handle_type_t* type);

/** Destroy every live resource owned by `owner`. Called at slot teardown. */
void wos_handles_destroy_owned(wos_slot_t owner);
