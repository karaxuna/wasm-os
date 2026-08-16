#include "esp_log.h"
#include "wasm_export.h"

#include "common.h"
#include "modules.h"

static const char* TAG = "wasm_output";

/* Structured (JSON) output channel: the app emits, the host logs it to serial. */
static int32_t wasm_output_emit(wasm_exec_env_t exec_env, char* json_data) {
  if (!json_data) {
    return WOS_ERR_INVALID_ARG;
  }
  ESP_LOGI(TAG, "%s", json_data);
  return WOS_OK;
}

static NativeSymbol k_symbols[] = {
    {"output_emit", wasm_output_emit, "($)i", NULL},
};

bool wos_register_output(void) {
  return wasm_runtime_register_natives("output", k_symbols, sizeof(k_symbols) / sizeof(k_symbols[0]));
}
