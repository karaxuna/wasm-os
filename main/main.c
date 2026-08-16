#include <locale.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/task.h"

#include "app_runtime.h"
#include "device_config.h"
#include "logger.h"
#include "serial_cmd.h"

#define MEMORY_REPORT_INTERVAL_MS 10000

static const char* TAG = "main";

static void init_littlefs(void) {
  esp_vfs_littlefs_conf_t conf = {
      .base_path = "/littlefs",
      .partition_label = "littlefs",
      .format_if_mount_failed = true,
  };
  ESP_ERROR_CHECK(esp_vfs_littlefs_register(&conf));

  size_t total = 0, used = 0;
  if (esp_littlefs_info(conf.partition_label, &total, &used) == ESP_OK) {
    ESP_LOGI(TAG, "LittleFS: %u/%u bytes used", (unsigned)used, (unsigned)total);
  }
}

void app_main(void) {
  setlocale(LC_ALL, "");
  logger_init();

  init_littlefs();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  device_config_load();

  /* Serial handler first: the device must respond to the CLI even while
   * WiFi is still connecting (or failing to). */
  app_runtime_init();
  ESP_ERROR_CHECK(serial_cmd_init());

  /* WiFi is app-initiated: the guest connects through the "wifi" binding. */
  if (esp_reset_reason() == ESP_RST_PANIC) {
    ESP_LOGW(TAG, "Previous run panicked; not auto-starting the app to avoid a crash loop");
  } else {
    app_runtime_start();
  }

  while (true) {
    size_t total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "Heap: %u/%u bytes free, app %s", (unsigned)free_bytes, (unsigned)total,
             app_runtime_is_running() ? "running" : "stopped");
    vTaskDelay(pdMS_TO_TICKS(MEMORY_REPORT_INTERVAL_MS));
  }
}
