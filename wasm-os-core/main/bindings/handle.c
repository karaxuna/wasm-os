#include "handle.h"

#include "common.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/semphr.h"

static const char* TAG = "wos_handle";

#define HANDLE_TABLE_SIZE 96
#define HANDLE_INDEX_BITS 8
#define HANDLE_INDEX_MASK ((1u << HANDLE_INDEX_BITS) - 1u)

/* handle = generation << HANDLE_INDEX_BITS | index; generation starts at 1,
 * so a valid handle is never 0. */

typedef struct {
  const wos_handle_type_t* type; /* NULL = free slot */
  void* ptr;
  uint32_t generation;
  wos_slot_t owner;
} handle_slot_t;

static handle_slot_t s_slots[HANDLE_TABLE_SIZE];
static SemaphoreHandle_t s_lock = NULL;

static uint32_t next_generation(uint32_t generation) {
  generation = (generation + 1) & (UINT32_MAX >> HANDLE_INDEX_BITS);
  return generation == 0 ? 1 : generation;
}

void wos_handles_init(void) {
  if (s_lock == NULL) {
    s_lock = xSemaphoreCreateMutex();
    for (int i = 0; i < HANDLE_TABLE_SIZE; i++) {
      s_slots[i].generation = 1;
    }
  }
}

wos_handle_t wos_handle_create(const wos_handle_type_t* type, void* ptr) {
  if (!type || !ptr || !s_lock) {
    return WOS_HANDLE_INVALID;
  }

  wos_slot_t owner = wos_owner_current();

  wos_handle_t handle = WOS_HANDLE_INVALID;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  for (int i = 0; i < HANDLE_TABLE_SIZE; i++) {
    if (s_slots[i].type == NULL) {
      s_slots[i].type = type;
      s_slots[i].ptr = ptr;
      s_slots[i].owner = owner;
      handle = (s_slots[i].generation << HANDLE_INDEX_BITS) | (uint32_t)i;
      break;
    }
  }
  xSemaphoreGive(s_lock);

  if (handle == WOS_HANDLE_INVALID) {
    ESP_LOGE(TAG, "Handle table full (%d entries), cannot register %s", HANDLE_TABLE_SIZE, type->name);
  }
  return handle;
}

/* Returns the slot for a valid live handle of the expected type, else NULL.
 * Caller must hold s_lock. */
static handle_slot_t* resolve_locked(wos_handle_t handle, const wos_handle_type_t* type) {
  uint32_t index = handle & HANDLE_INDEX_MASK;
  uint32_t generation = handle >> HANDLE_INDEX_BITS;
  if (index >= HANDLE_TABLE_SIZE) {
    return NULL;
  }
  handle_slot_t* slot = &s_slots[index];
  if (slot->type != type || slot->generation != generation) {
    return NULL;
  }
  return slot;
}

void* wos_handle_deref(wos_handle_t handle, const wos_handle_type_t* type) {
  if (!type || !s_lock) {
    return NULL;
  }

  xSemaphoreTake(s_lock, portMAX_DELAY);
  handle_slot_t* slot = resolve_locked(handle, type);
  void* ptr = slot ? slot->ptr : NULL;
  xSemaphoreGive(s_lock);

  if (!ptr) {
    ESP_LOGE(TAG, "Invalid %s handle: 0x%08x", type->name, (unsigned)handle);
  }
  return ptr;
}

int32_t wos_handle_destroy(wos_handle_t handle, const wos_handle_type_t* type) {
  if (!type || !s_lock) {
    return WOS_ERR_INVALID_HANDLE;
  }

  xSemaphoreTake(s_lock, portMAX_DELAY);
  handle_slot_t* slot = resolve_locked(handle, type);
  void* ptr = NULL;
  if (slot) {
    ptr = slot->ptr;
    slot->type = NULL;
    slot->ptr = NULL;
    slot->generation = next_generation(slot->generation);
  }
  xSemaphoreGive(s_lock);

  if (!slot) {
    ESP_LOGE(TAG, "Invalid %s handle: 0x%08x", type->name, (unsigned)handle);
    return WOS_ERR_INVALID_HANDLE;
  }
  if (type->destroy) {
    type->destroy(ptr);
  }
  return WOS_OK;
}

void wos_handles_destroy_owned(wos_slot_t owner) {
  if (!s_lock) {
    return;
  }

  int leaked = 0;
  for (int i = 0; i < HANDLE_TABLE_SIZE; i++) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const wos_handle_type_t* type = s_slots[i].type;
    void* ptr = s_slots[i].ptr;
    if (type && s_slots[i].owner == owner) {
      s_slots[i].type = NULL;
      s_slots[i].ptr = NULL;
      s_slots[i].generation = next_generation(s_slots[i].generation);
    } else {
      type = NULL;
    }
    xSemaphoreGive(s_lock);

    if (type) {
      ESP_LOGD(TAG, "Releasing leaked %s", type->name);
      if (type->destroy) {
        type->destroy(ptr);
      }
      leaked++;
    }
  }

  if (leaked > 0) {
    ESP_LOGW(TAG, "Released %d host resource(s) leaked by the app", leaked);
  }
}
