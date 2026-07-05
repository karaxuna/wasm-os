#include "esp_heap_caps.h"

void* wasm_malloc(size_t size) {
  return heap_caps_aligned_alloc(8, size, MALLOC_CAP_DEFAULT);
}

void wasm_free(void* ptr) {
  heap_caps_free(ptr);
}

void* wasm_realloc(void* ptr, size_t new_size) {
  return heap_caps_realloc(ptr, new_size, MALLOC_CAP_DEFAULT);
}
