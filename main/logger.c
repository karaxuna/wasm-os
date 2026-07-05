#include "logger.h"

#include <stdarg.h>
#include <stdio.h>

static esp_log_level_t current_log_level = ESP_LOG_INFO;

void logger_init(void) {
  esp_log_level_set("*", current_log_level);
}

void logger_set_level(esp_log_level_t level) {
  current_log_level = level;
  esp_log_level_set("*", level);
}

esp_log_level_t logger_get_level(void) {
  return current_log_level;
}
