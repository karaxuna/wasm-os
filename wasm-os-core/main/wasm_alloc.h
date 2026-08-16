#include <sys/cdefs.h>
void* wasm_malloc(size_t size);
void wasm_free(void* ptr);
void* wasm_realloc(void* ptr, size_t new_size);
