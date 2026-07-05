#pragma once

#include <stdbool.h>

#include "esp_err.h"

/**
 * Lifecycle of the WASM application (/littlefs/main.wasm).
 *
 * The app runs on its own FreeRTOS task, which is the sole owner of every
 * WAMR object (runtime, module, instance, exec env). Other tasks only
 * *request* a stop: app_runtime_stop() interrupts guest execution via
 * wasm_runtime_terminate() and waits for the app task to tear itself down,
 * so the runtime is never freed underneath running code.
 */

/** One-time initialization. Call before any other app_runtime function. */
void app_runtime_init(void);

/**
 * Launch the app task. Returns ESP_ERR_INVALID_STATE if already running,
 * ESP_ERR_NO_MEM if the task could not be created.
 */
esp_err_t app_runtime_start(void);

/**
 * Request a stop and block until the app has fully torn down (bounded wait).
 * Returns ESP_OK when stopped (or when nothing was running), ESP_ERR_TIMEOUT
 * if the app task had to be force-deleted (resources may leak; a reboot is
 * advisable before the next start).
 */
esp_err_t app_runtime_stop(void);

bool app_runtime_is_running(void);
