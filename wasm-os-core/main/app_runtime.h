#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "bindings/owner.h"

/**
 * Two-slot WASM app runtime.
 *
 * The WAMR runtime itself is initialized once at boot and never destroyed;
 * instances come and go against it. WOS_SLOT_MAIN boots /littlefs/main.wasm
 * and is what the serial protocol manages; WOS_SLOT_CHILD is started only
 * through the `app` binding, by the main app.
 *
 * Per slot, the invariant is unchanged from the single-app design: the
 * slot's task is the sole owner of that slot's WAMR objects. Nothing frees
 * another slot's state; stopping is request-and-wait, with a force-delete
 * last resort that leaks (and logs) the slot's resources.
 */

typedef enum {
  APP_STATE_STOPPED = 0,
  APP_STATE_STARTING = 1,
  APP_STATE_RUNNING = 2,
  APP_STATE_STOPPING = 3,
  /* Entry trapped; the instance stays live (spawned tasks may still run)
   * until a stop reclaims the slot. */
  APP_STATE_CRASHED = 4,
} app_state_t;

/** Initialize WAMR and register all bindings. Aborts on failure. */
void app_runtime_init(void);

/**
 * Start `path` (NULL = /littlefs/main.wasm) in `slot`. Fails with
 * ESP_ERR_INVALID_STATE unless the slot is stopped.
 */
esp_err_t app_runtime_start(wos_slot_t slot, const char* path);

/**
 * Request a stop and wait up to `timeout_ms` (0 = default 10 s) for the
 * slot's own teardown. On timeout the slot's task is force-deleted and its
 * binding state reclaimed best-effort; WAMR objects leak (logged).
 */
esp_err_t app_runtime_stop(wos_slot_t slot, uint32_t timeout_ms);

app_state_t app_runtime_state(wos_slot_t slot);

/** True while the slot has a live task (any state but STOPPED). */
bool app_runtime_is_running(wos_slot_t slot);

/**
 * Copy the slot's last trap/exit message into buf (snprintf-style: returns
 * the message length, copies when cap is large enough). 0 when none.
 */
int32_t app_runtime_last_error(wos_slot_t slot, char* buf, size_t cap);
