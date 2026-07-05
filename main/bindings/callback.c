#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/semphr.h"
#include "wasm_export.h"

#include "common.h"
#include "modules.h"

/*
 * Cross-module callback registry: an app registers a function by its table
 * index and receives a small handle any code path can later invoke it with.
 * Invocation always runs on the *caller's* exec env, so it is safe from any
 * task that owns one.
 */

#define MAX_CALLBACKS 32
#define MAX_CALLBACK_ARGS 8
#define CALLBACK_INVALID UINT32_MAX

static const char* TAG = "wasm_callback";

typedef struct {
  uint32_t table_idx;
  bool in_use;
} callback_entry_t;

static callback_entry_t s_callbacks[MAX_CALLBACKS];
static SemaphoreHandle_t s_lock;

static uint32_t wasm_callback_register(wasm_exec_env_t exec_env, uint32_t table_idx) {
  uint32_t handle = CALLBACK_INVALID;

  xSemaphoreTake(s_lock, portMAX_DELAY);
  for (uint32_t i = 0; i < MAX_CALLBACKS; i++) {
    if (!s_callbacks[i].in_use) {
      s_callbacks[i].table_idx = table_idx;
      s_callbacks[i].in_use = true;
      handle = i;
      break;
    }
  }
  xSemaphoreGive(s_lock);

  if (handle == CALLBACK_INVALID) {
    ESP_LOGE(TAG, "No free callback slots (max %d)", MAX_CALLBACKS);
  }
  return handle;
}

/*
 * Invoke a registered callback with up to MAX_CALLBACK_ARGS u32 arguments
 * read from guest memory. Returns the callback's first result (0 for void
 * callbacks), or UINT32_MAX on error.
 */
static uint32_t wasm_callback_invoke(wasm_exec_env_t exec_env, uint32_t handle, uint32_t args_aptr, uint32_t n_args) {
  if (n_args > MAX_CALLBACK_ARGS) {
    ESP_LOGE(TAG, "Too many callback args: %u (max %d)", (unsigned)n_args, MAX_CALLBACK_ARGS);
    return CALLBACK_INVALID;
  }

  xSemaphoreTake(s_lock, portMAX_DELAY);
  bool valid = handle < MAX_CALLBACKS && s_callbacks[handle].in_use;
  uint32_t table_idx = valid ? s_callbacks[handle].table_idx : 0;
  xSemaphoreGive(s_lock);

  if (!valid) {
    ESP_LOGE(TAG, "Invalid callback handle: %u", (unsigned)handle);
    return CALLBACK_INVALID;
  }

  uint32_t argv[MAX_CALLBACK_ARGS > 0 ? MAX_CALLBACK_ARGS : 1] = {0};
  if (n_args > 0) {
    const uint32_t* args = wos_guest_ptr(exec_env, args_aptr, (uint64_t)n_args * sizeof(uint32_t));
    if (!args) {
      ESP_LOGE(TAG, "Callback args out of bounds: addr=%u count=%u", (unsigned)args_aptr, (unsigned)n_args);
      return CALLBACK_INVALID;
    }
    memcpy(argv, args, n_args * sizeof(uint32_t));
  }

  if (!wasm_runtime_call_indirect(exec_env, table_idx, n_args, argv)) {
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    ESP_LOGE(TAG, "Callback %u trapped: %s", (unsigned)handle, wasm_runtime_get_exception(inst));
    return CALLBACK_INVALID;
  }
  return argv[0];
}

static void wasm_callback_unregister(wasm_exec_env_t exec_env, uint32_t handle) {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  if (handle < MAX_CALLBACKS) {
    s_callbacks[handle].in_use = false;
  }
  xSemaphoreGive(s_lock);
}

void wos_callbacks_reset(void) {
  if (!s_lock) {
    return;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  memset(s_callbacks, 0, sizeof(s_callbacks));
  xSemaphoreGive(s_lock);
}

static NativeSymbol k_symbols[] = {
    {"callback_register", wasm_callback_register, "(i)i", NULL},
    {"callback_invoke", wasm_callback_invoke, "(iii)i", NULL},
    {"callback_unregister", wasm_callback_unregister, "(i)", NULL},
};

bool wos_register_callback(void) {
  if (!s_lock) {
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
      return false;
    }
  }
  return wasm_runtime_register_natives("callback", k_symbols, sizeof(k_symbols) / sizeof(k_symbols[0]));
}
