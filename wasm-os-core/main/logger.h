#pragma once

#include "esp_log.h"

void logger_init(void);
void logger_set_level(esp_log_level_t level);
esp_log_level_t logger_get_level(void);
