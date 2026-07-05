#include "device_config.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "logger.h"
#include "storage.h"

#define CONFIG_NAMESPACE "config"

static const char* TAG = "device_config";

static device_config_t s_config;

/*
 * Split `str` on any of the characters in `delims`, skipping empty tokens.
 * Returns a malloc'd array of malloc'd copies (or NULL when there are none).
 */
static char** split(const char* str, const char* delims, uint32_t* out_count) {
  *out_count = 0;

  char* work = strdup(str);
  if (!work) {
    return NULL;
  }

  uint32_t count = 0;
  for (char* token = strtok(work, delims); token; token = strtok(NULL, delims)) {
    count++;
  }
  free(work);

  if (count == 0) {
    return NULL;
  }

  char** tokens = calloc(count, sizeof(char*));
  work = strdup(str);
  if (!tokens || !work) {
    free(tokens);
    free(work);
    return NULL;
  }

  uint32_t i = 0;
  for (char* token = strtok(work, delims); token && i < count; token = strtok(NULL, delims)) {
    tokens[i] = strdup(token);
    if (!tokens[i]) {
      while (i > 0) {
        free(tokens[--i]);
      }
      free(tokens);
      free(work);
      return NULL;
    }
    i++;
  }
  free(work);

  *out_count = i;
  return tokens;
}

void device_config_load(void) {
  if (storage_get_string(CONFIG_NAMESPACE, "ssid", &s_config.wifi_ssid) != ESP_OK) {
    s_config.wifi_ssid = NULL;
  }
  if (storage_get_string(CONFIG_NAMESPACE, "pass", &s_config.wifi_pass) != ESP_OK) {
    s_config.wifi_pass = NULL;
  }

  char* env_str = NULL;
  if (storage_get_string(CONFIG_NAMESPACE, "env", &env_str) == ESP_OK) {
    s_config.env = split(env_str, ";", &s_config.env_count);
    free(env_str);
    ESP_LOGI(TAG, "Loaded %u environment variable(s)", (unsigned)s_config.env_count);
  }

  uint8_t log_level = 0;
  if (storage_get_u8(CONFIG_NAMESPACE, "log_level", &log_level) == ESP_OK) {
    logger_set_level((esp_log_level_t)log_level);
  }
}

const device_config_t* device_config_get(void) {
  return &s_config;
}
