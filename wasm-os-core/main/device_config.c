#include "device_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "logger.h"

#define ENV_PATH "/littlefs/.env"
#define ENV_MAX_BYTES 8192
#define ENV_MAX_VARS 64

static const char* TAG = "device_config";

static device_config_t s_config;

/* Trim leading and trailing whitespace in place; returns the first non-space. */
static char* trim(char* s) {
  while (*s && isspace((unsigned char)*s)) {
    s++;
  }

  char* end = s + strlen(s);
  while (end > s && isspace((unsigned char)end[-1])) {
    *--end = '\0';
  }

  return s;
}

/* Drop one layer of matching single or double quotes around a value. */
static char* unquote(char* s) {
  size_t len = strlen(s);
  if (len >= 2 && (s[0] == '"' || s[0] == '\'') && s[len - 1] == s[0]) {
    s[len - 1] = '\0';
    return s + 1;
  }

  return s;
}

static char* read_file(const char* path) {
  FILE* file = fopen(path, "rb");
  if (!file) {
    return NULL;
  }

  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (size < 0 || size > ENV_MAX_BYTES) {
    ESP_LOGW(TAG, "%s is %ld bytes, max is %d", path, size, ENV_MAX_BYTES);
    fclose(file);
    return NULL;
  }

  char* text = malloc((size_t)size + 1);
  if (!text) {
    fclose(file);
    return NULL;
  }

  size_t got = fread(text, 1, (size_t)size, file);
  fclose(file);
  text[got] = '\0';

  return text;
}

/* Join "KEY" and "VALUE" into the "KEY=VALUE" form WASI expects. */
static char* join_pair(const char* key, const char* value) {
  size_t len = strlen(key) + strlen(value) + 2;
  char* pair = malloc(len);
  if (!pair) {
    return NULL;
  }

  snprintf(pair, len, "%s=%s", key, value);
  return pair;
}

void device_config_load(void) {
  char* text = read_file(ENV_PATH);
  if (!text) {
    ESP_LOGI(TAG, "No %s found, running with defaults", ENV_PATH);
    return;
  }

  char** env = calloc(ENV_MAX_VARS, sizeof(char*));
  if (!env) {
    free(text);
    return;
  }

  uint32_t env_count = 0;
  char* save = NULL;

  for (char* line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
    char* entry = trim(line);
    if (*entry == '\0' || *entry == '#') {
      continue;
    }

    char* sep = strchr(entry, '=');
    if (!sep) {
      ESP_LOGW(TAG, "Ignoring malformed line: %s", entry);
      continue;
    }

    *sep = '\0';
    char* key = trim(entry);
    char* value = unquote(trim(sep + 1));

    if (strcmp(key, "LOG_LEVEL") == 0) {
      logger_set_level((esp_log_level_t)atoi(value));
    } else if (env_count < ENV_MAX_VARS) {
      char* pair = join_pair(key, value);
      if (pair) {
        env[env_count++] = pair;
      }
    } else {
      ESP_LOGW(TAG, "Too many variables, ignoring %s", key);
    }
  }

  free(text);

  s_config.env = env;
  s_config.env_count = env_count;
  ESP_LOGI(TAG, "Loaded %s: %u environment variable(s)", ENV_PATH, (unsigned)env_count);
}

const device_config_t* device_config_get(void) {
  return &s_config;
}
