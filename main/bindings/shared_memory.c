#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/semphr.h"
#include "wasm_export.h"

#include "common.h"
#include "modules.h"

/*
 * Memory-sharing registry: an app exports a region of its linear memory by
 * address, other code copies from it by that address. Only guest addresses
 * and lengths are stored; native pointers are resolved (and re-validated) at
 * copy time, so the registry stays correct even if linear memory moves.
 */

#define MAX_SHARED_REGIONS 8

static const char* TAG = "wasm_shared_memory";

typedef struct {
  wasm_module_inst_t owner;
  uint64_t addr; /* guest address in the owner's memory */
  uint32_t len;
  bool in_use;
} shared_region_t;

static shared_region_t s_regions[MAX_SHARED_REGIONS];
static SemaphoreHandle_t s_lock;

static shared_region_t* find_locked(uint64_t addr) {
  for (int i = 0; i < MAX_SHARED_REGIONS; i++) {
    if (s_regions[i].in_use && s_regions[i].addr == addr) {
      return &s_regions[i];
    }
  }
  return NULL;
}

static int32_t wasm_share_memory(wasm_exec_env_t exec_env, uint64_t addr, uint32_t size) {
  wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
  if (!inst || size == 0) {
    return WOS_ERR_INVALID_ARG;
  }
  if (!wasm_runtime_validate_app_addr(inst, addr, size)) {
    ESP_LOGE(TAG, "Shared region out of bounds: addr=%llu size=%u", (unsigned long long)addr, (unsigned)size);
    return WOS_ERR_BAD_MEMORY;
  }

  int32_t result = WOS_ERR_NO_MEM;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  if (find_locked(addr)) {
    result = WOS_ERR_INVALID_ARG; /* already shared */
  } else {
    for (int i = 0; i < MAX_SHARED_REGIONS; i++) {
      if (!s_regions[i].in_use) {
        s_regions[i] = (shared_region_t){.owner = inst, .addr = addr, .len = size, .in_use = true};
        result = WOS_OK;
        break;
      }
    }
  }
  xSemaphoreGive(s_lock);
  return result;
}

static int32_t wasm_copy_shared_memory(wasm_exec_env_t exec_env, uint64_t from_addr, uint64_t to_addr, uint32_t offset,
                                       uint32_t size) {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  shared_region_t* region = find_locked(from_addr);
  shared_region_t snapshot = region ? *region : (shared_region_t){0};
  xSemaphoreGive(s_lock);

  if (!region) {
    return WOS_ERR_NOT_FOUND;
  }
  if (offset > snapshot.len || size > snapshot.len - offset) {
    ESP_LOGE(TAG, "Copy range exceeds shared region: offset=%u size=%u len=%u", (unsigned)offset, (unsigned)size,
             (unsigned)snapshot.len);
    return WOS_ERR_BAD_MEMORY;
  }

  if (!wasm_runtime_validate_app_addr(snapshot.owner, snapshot.addr + offset, size)) {
    return WOS_ERR_BAD_MEMORY;
  }
  const void* src = wasm_runtime_addr_app_to_native(snapshot.owner, snapshot.addr + offset);
  void* dst = wos_guest_ptr(exec_env, to_addr, size);
  if (!src || !dst) {
    return WOS_ERR_BAD_MEMORY;
  }

  memcpy(dst, src, size);
  return WOS_OK;
}

static uint32_t wasm_get_shared_memory_len(wasm_exec_env_t exec_env, uint64_t addr) {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  shared_region_t* region = find_locked(addr);
  uint32_t len = region ? region->len : 0;
  xSemaphoreGive(s_lock);
  return len;
}

static int32_t wasm_remove_memory_sharing(wasm_exec_env_t exec_env, uint64_t addr) {
  wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);

  int32_t result;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  shared_region_t* region = find_locked(addr);
  if (!region) {
    result = WOS_ERR_NOT_FOUND;
  } else if (region->owner != inst) {
    result = WOS_ERR_INVALID_ARG; /* only the owner may unshare */
  } else {
    region->in_use = false;
    result = WOS_OK;
  }
  xSemaphoreGive(s_lock);
  return result;
}

void wos_shared_memory_reset(void) {
  if (!s_lock) {
    return;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  memset(s_regions, 0, sizeof(s_regions));
  xSemaphoreGive(s_lock);
}

static NativeSymbol k_symbols[] = {
    {"share_memory", wasm_share_memory, "(Ii)i", NULL},
    {"copy_shared_memory", wasm_copy_shared_memory, "(IIii)i", NULL},
    {"get_shared_memory_len", wasm_get_shared_memory_len, "(I)i", NULL},
    {"remove_memory_sharing", wasm_remove_memory_sharing, "(I)i", NULL},
};

bool wos_register_shared_memory(void) {
  if (!s_lock) {
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
      return false;
    }
  }
  return wasm_runtime_register_natives("shared_memory", k_symbols, sizeof(k_symbols) / sizeof(k_symbols[0]));
}
