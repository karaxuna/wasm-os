#pragma once

#include "esp_task.h"
#include "freertos/FreeRTOS.h"

/**
 * The single place task priorities are chosen.
 *
 * FreeRTOS always runs the highest-priority ready task, and time-slices only
 * between equal priorities. A guest that never yields therefore starves every
 * lower-priority task outright — on a single-core part (C3/C6) there is no
 * second core to hide it. So the serial command handler must outrank anything
 * the guest controls, or a busy app makes the device unreachable over serial
 * and only a reflash recovers it.
 *
 * The ordering that matters:
 *
 *   guest tasks <= app < serial handler < ESP-IDF system tasks
 *
 * Define new priorities relative to these rather than as bare numbers, and let
 * the static assertions below hold the ordering.
 */

/* ESP-IDF's own tasks (WiFi, IPC, timers) sit near the top of the range. The
 * serial handler stays below them: console traffic must never delay the radio. */
#define WOS_PRIO_SYSTEM_FLOOR (ESP_TASK_PRIO_MAX - 5)

#define WOS_PRIO_SERIAL_CMD 12
#define WOS_PRIO_APP 10

/* Ceiling for tasks a guest spawns through the `task` binding. */
#define WOS_PRIO_GUEST_MAX 10

_Static_assert(WOS_PRIO_APP < WOS_PRIO_SERIAL_CMD,
               "the serial handler must preempt the app, or a busy guest makes the device unreachable");
_Static_assert(WOS_PRIO_GUEST_MAX < WOS_PRIO_SERIAL_CMD,
               "guest tasks must never outrank the serial handler");
_Static_assert(WOS_PRIO_SERIAL_CMD < WOS_PRIO_SYSTEM_FLOOR,
               "the serial handler must stay below ESP-IDF's system tasks");
_Static_assert(WOS_PRIO_SYSTEM_FLOOR < configMAX_PRIORITIES, "priority range must fit configMAX_PRIORITIES");
