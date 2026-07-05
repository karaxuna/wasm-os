#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * Thin typed wrappers over NVS. Getters that return heap memory (the string
 * and blob variants) allocate with malloc; the caller owns the result.
 */

esp_err_t storage_get_string(const char* ns, const char* key, char** out);
esp_err_t storage_set_string(const char* ns, const char* key, const char* value);

esp_err_t storage_get_u32(const char* ns, const char* key, uint32_t* out);
esp_err_t storage_set_u32(const char* ns, const char* key, uint32_t value);

esp_err_t storage_get_u8(const char* ns, const char* key, uint8_t* out);
esp_err_t storage_set_u8(const char* ns, const char* key, uint8_t value);

esp_err_t storage_get_blob(const char* ns, const char* key, void** out, size_t* out_size);
esp_err_t storage_set_blob(const char* ns, const char* key, const void* data, size_t size);
