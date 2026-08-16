#include "bindings.h"

#include "esp_log.h"
#include "handle.h"
#include "modules.h"

static const char* TAG = "bindings";

typedef struct {
  const char* name;
  bool (*fn)(void);
} registrar_t;

static const registrar_t k_registrars[] = {
    {"callback", wos_register_callback},
    {"env", wos_register_env},
    {"esp_lcd", wos_register_esp_lcd},
    {"fs", wos_register_fs},
    {"gfx", wos_register_gfx},
    {"gpio", wos_register_gpio},
    {"http_client", wos_register_http_client},
    {"i2c_master", wos_register_i2c_master},
    {"i2s_std", wos_register_i2s_std},
    {"output", wos_register_output},
    {"shared_memory", wos_register_shared_memory},
    {"socket", wos_register_socket},
    {"spi_master", wos_register_spi_master},
    {"task", wos_register_task},
    {"wasi", wos_register_wasi},
    {"websocket", wos_register_websocket},
};

bool wos_register_all_bindings(void) {
  wos_handles_init();

  for (size_t i = 0; i < sizeof(k_registrars) / sizeof(k_registrars[0]); i++) {
    if (!k_registrars[i].fn()) {
      ESP_LOGE(TAG, "Failed to register %s bindings", k_registrars[i].name);
      return false;
    }
  }
  return true;
}

void wos_bindings_reset(void) {
  /* Tasks first: stragglers may still be using handles or shared memory. */
  wos_tasks_reset();
  wos_handles_destroy_all();
  /* After the handle table: leaked I2C devices must be removed before their
   * bus can be deleted. */
  wos_i2c_master_reset();
  wos_callbacks_reset();
  wos_shared_memory_reset();
}

int wos_bindings_active_tasks(void) {
  return wos_tasks_active();
}
