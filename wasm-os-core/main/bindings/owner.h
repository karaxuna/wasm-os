#pragma once

#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/task.h"

/**
 * App slots and the task-to-slot registry behind resource ownership.
 *
 * Every guest-visible resource (handles, callbacks, shared regions, spawned
 * tasks, I2C buses) is stamped with the slot that created it, so teardown of
 * one slot never touches the other's state. The creating slot is derived
 * from the calling FreeRTOS task: app tasks register themselves at slot
 * start, and the task binding registers guest-spawned tasks under the
 * spawner's slot. Host tasks that were never registered (serial handler,
 * boot) resolve to WOS_SLOT_MAIN.
 */

typedef enum {
  WOS_SLOT_MAIN = 0,  /* /littlefs/main.wasm, auto-started at boot */
  WOS_SLOT_CHILD = 1, /* started only through the app binding */
  WOS_SLOT_COUNT = 2,
} wos_slot_t;

void wos_owner_init(void);

/** Associate `task` with `slot`. No-op when the registry is full. */
void wos_owner_bind(TaskHandle_t task, wos_slot_t slot);

void wos_owner_unbind(TaskHandle_t task);

/** The slot the calling task belongs to; WOS_SLOT_MAIN when unregistered. */
wos_slot_t wos_owner_current(void);
