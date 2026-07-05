#include "driver/gpio.h"
#include "wasm_export.h"

#include "common.h"
#include "modules.h"

static int32_t wasm_gpio_set_level(wasm_exec_env_t exec_env, int32_t gpio_num, uint32_t level) {
  return wos_err(gpio_set_level((gpio_num_t)gpio_num, level));
}

static int32_t wasm_gpio_get_level(wasm_exec_env_t exec_env, int32_t gpio_num) {
  return gpio_get_level((gpio_num_t)gpio_num);
}

static int32_t wasm_gpio_set_direction(wasm_exec_env_t exec_env, int32_t gpio_num, int32_t mode) {
  return wos_err(gpio_set_direction((gpio_num_t)gpio_num, (gpio_mode_t)mode));
}

static int32_t wasm_gpio_reset_pin(wasm_exec_env_t exec_env, int32_t gpio_num) {
  return wos_err(gpio_reset_pin((gpio_num_t)gpio_num));
}

static int32_t wasm_gpio_config(wasm_exec_env_t exec_env, uint64_t pin_bit_mask, int32_t mode, int32_t pull_up_en,
                                int32_t pull_down_en, int32_t intr_type) {
  gpio_config_t io_conf = {
      .pin_bit_mask = pin_bit_mask,
      .mode = (gpio_mode_t)mode,
      .pull_up_en = (gpio_pullup_t)pull_up_en,
      .pull_down_en = (gpio_pulldown_t)pull_down_en,
      .intr_type = (gpio_int_type_t)intr_type,
  };
  return wos_err(gpio_config(&io_conf));
}

static NativeSymbol k_symbols[] = {
    {"gpio_set_level", wasm_gpio_set_level, "(ii)i", NULL},
    {"gpio_get_level", wasm_gpio_get_level, "(i)i", NULL},
    {"gpio_set_direction", wasm_gpio_set_direction, "(ii)i", NULL},
    {"gpio_reset_pin", wasm_gpio_reset_pin, "(i)i", NULL},
    {"gpio_config", wasm_gpio_config, "(Iiiii)i", NULL},
};

bool wos_register_gpio(void) {
  return wasm_runtime_register_natives("gpio", k_symbols, sizeof(k_symbols) / sizeof(k_symbols[0]));
}
