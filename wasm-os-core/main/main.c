#include <locale.h>

#include "esp_attr.h"
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

/* Consecutive-panic budget: below the limit the app still auto-starts (its
 * crash policy is the app's own business, exposed via app_reset_reason);
 * at the limit, hold off and wait for serial recovery. */
#define PANIC_MAGIC 0x574F5321u /* "WOS!" */
#define PANIC_LIMIT 3

RTC_NOINIT_ATTR static uint32_t s_panic_magic;
RTC_NOINIT_ATTR static uint32_t s_panic_count;

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
    if (s_panic_magic != PANIC_MAGIC) {
      s_panic_magic = PANIC_MAGIC;
      s_panic_count = 0;
    }
    s_panic_count++;
    if (s_panic_count >= PANIC_LIMIT) {
      ESP_LOGW(TAG, "Panicked %u times in a row; not auto-starting the app to break the crash loop",
               (unsigned)s_panic_count);
    } else {
      ESP_LOGW(TAG, "Previous run panicked (%u/%u); starting the app anyway", (unsigned)s_panic_count, PANIC_LIMIT);
      app_runtime_start(WOS_SLOT_MAIN, NULL);
    }
  } else {
    s_panic_magic = PANIC_MAGIC;
    s_panic_count = 0;
    app_runtime_start(WOS_SLOT_MAIN, NULL);
  }

  while (true) {
    size_t total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "Heap: %u/%u bytes free, main %s, child %s", (unsigned)free_bytes, (unsigned)total,
             app_runtime_is_running(WOS_SLOT_MAIN) ? "running" : "stopped",
             app_runtime_is_running(WOS_SLOT_CHILD) ? "running" : "stopped");
    vTaskDelay(pdMS_TO_TICKS(MEMORY_REPORT_INTERVAL_MS));
  }
}
