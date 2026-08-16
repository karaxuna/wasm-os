#pragma once

#include "esp_task.h"
#include "freertos/FreeRTOS.h"

/**
 * The single place task priorities are chosen.
 *
 * FreeRTOS always runs the highest-priority ready task, and time-slices only
 * between equal priorities. A guest that never yields therefore starves every
 * lower-priority task outright — on a single-core part (C3/C6) there is no
 * second core to hide it. Two orderings matter:
 *
 *   - The serial command handler must outrank anything a guest controls, or
 *     a busy app makes the device unreachable over serial.
 *   - The main app (the supervisor on managed devices) must outrank the
 *     child app it manages, or a spinning child starves its manager and
 *     remote recovery is impossible.
 *
 *   child guests <= child < main guests <= main < serial < ESP-IDF system
 *
 * Define new priorities relative to these rather than as bare numbers, and let
 * the static assertions below hold the ordering.
 */

/* ESP-IDF's own tasks (WiFi, IPC, timers) sit near the top of the range. The
 * serial handler stays below them: console traffic must never delay the radio. */
#define WOS_PRIO_SYSTEM_FLOOR (ESP_TASK_PRIO_MAX - 5)

#define WOS_PRIO_SERIAL_CMD 12
#define WOS_PRIO_APP_MAIN 10
#define WOS_PRIO_APP_CHILD 9

/* Ceiling for tasks a guest spawns through the `task` binding: a guest may
 * never outrank its own app task. */
#define WOS_PRIO_GUEST_MAX_MAIN WOS_PRIO_APP_MAIN
#define WOS_PRIO_GUEST_MAX_CHILD WOS_PRIO_APP_CHILD

_Static_assert(WOS_PRIO_APP_CHILD < WOS_PRIO_APP_MAIN,
               "the main app must preempt the child, or a spinning child makes its manager unresponsive");
_Static_assert(WOS_PRIO_APP_MAIN < WOS_PRIO_SERIAL_CMD,
               "the serial handler must preempt the apps, or a busy guest makes the device unreachable");
_Static_assert(WOS_PRIO_GUEST_MAX_MAIN < WOS_PRIO_SERIAL_CMD,
               "guest tasks must never outrank the serial handler");
_Static_assert(WOS_PRIO_SERIAL_CMD < WOS_PRIO_SYSTEM_FLOOR,
               "the serial handler must stay below ESP-IDF's system tasks");
_Static_assert(WOS_PRIO_SYSTEM_FLOOR < configMAX_PRIORITIES, "priority range must fit configMAX_PRIORITIES");
