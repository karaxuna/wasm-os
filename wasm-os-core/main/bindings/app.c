#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "wasm_export.h"

#include "app_runtime.h"
#include "common.h"
#include "modules.h"
#include "owner.h"

/*
 * Child-app lifecycle management, callable only from the main slot: on
 * managed devices main.wasm is a supervisor and these are its levers. The
 * child cannot stop its manager or restart itself.
 */

#define STOP_TIMEOUT_MAX_MS 30000

static const char* TAG = "wasm_app";

static bool is_main_slot(void) {
  return wos_owner_current() == WOS_SLOT_MAIN;
}

/* Flat namespace under /littlefs, same rules as the serial protocol. */
static int32_t resolve_path(const char* name, char* out, size_t cap) {
  if (!name || name[0] == '\0') {
    return WOS_ERR_INVALID_ARG;
  }
  for (const char* c = name; *c; c++) {
    if (*c == '/' || *c == '\\') {
      return WOS_ERR_INVALID_ARG;
    }
  }
  if ((size_t)snprintf(out, cap, "/littlefs/%s", name) >= cap) {
    return WOS_ERR_INVALID_ARG;
  }
  return WOS_OK;
}

static int32_t wasm_app_start(wasm_exec_env_t exec_env, char* name) {
  if (!is_main_slot()) {
    return WOS_ERR_UNSUPPORTED;
  }

  char path[96];
  int32_t err = resolve_path(name, path, sizeof(path));
  if (err != WOS_OK) {
    return err;
  }

  ESP_LOGI(TAG, "Starting child app %s", path);
  return wos_err(app_runtime_start(WOS_SLOT_CHILD, path));
}

static int32_t wasm_app_stop(wasm_exec_env_t exec_env, int32_t timeout_ms) {
  if (!is_main_slot()) {
    return WOS_ERR_UNSUPPORTED;
  }
  if (timeout_ms < 0 || timeout_ms > STOP_TIMEOUT_MAX_MS) {
    return WOS_ERR_INVALID_ARG;
  }
  return wos_err(app_runtime_stop(WOS_SLOT_CHILD, (uint32_t)timeout_ms));
}

static int32_t wasm_app_status(wasm_exec_env_t exec_env) {
  if (!is_main_slot()) {
    return WOS_ERR_UNSUPPORTED;
  }
  return (int32_t)app_runtime_state(WOS_SLOT_CHILD);
}

static int32_t wasm_app_last_error(wasm_exec_env_t exec_env, uint32_t buf_aptr, uint32_t buf_cap) {
  if (!is_main_slot()) {
    return WOS_ERR_UNSUPPORTED;
  }
  char message[128];
  int32_t len = app_runtime_last_error(WOS_SLOT_CHILD, message, sizeof(message));
  return wos_guest_copy_out(exec_env, buf_aptr, buf_cap, message, (uint32_t)len);
}

/* Feeds the supervisor's crash-recovery policy after a whole-device reset. */
static int32_t wasm_app_reset_reason(wasm_exec_env_t exec_env) {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
    case ESP_RST_SW:
    case ESP_RST_DEEPSLEEP:
      return 0;
    case ESP_RST_PANIC:
      return 1;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      return 2;
    case ESP_RST_BROWNOUT:
      return 3;
    default:
      return 4;
  }
}

static NativeSymbol k_symbols[] = {
    {"app_start", wasm_app_start, "($)i", NULL},
    {"app_stop", wasm_app_stop, "(i)i", NULL},
    {"app_status", wasm_app_status, "()i", NULL},
    {"app_last_error", wasm_app_last_error, "(ii)i", NULL},
    {"app_reset_reason", wasm_app_reset_reason, "()i", NULL},
};

bool wos_register_app(void) {
  return wasm_runtime_register_natives("app", k_symbols, sizeof(k_symbols) / sizeof(k_symbols[0]));
}
