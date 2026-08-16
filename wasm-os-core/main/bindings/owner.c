#include "owner.h"

#include <stdbool.h>
#include <stddef.h>

#include "esp_log.h"

/* 2 app tasks + up to 8 guest-spawned tasks; a little headroom on top. */
#define OWNER_TABLE_SIZE 12

static const char* TAG = "wos_owner";

typedef struct {
  TaskHandle_t task; /* NULL = free */
  wos_slot_t slot;
} owner_entry_t;

static owner_entry_t s_owners[OWNER_TABLE_SIZE];
/* A spinlock, not a mutex: lookups happen inside other modules' critical
 * sections and must never block or yield. */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

void wos_owner_init(void) {
  taskENTER_CRITICAL(&s_lock);
  for (int i = 0; i < OWNER_TABLE_SIZE; i++) {
    s_owners[i].task = NULL;
  }
  taskEXIT_CRITICAL(&s_lock);
}

void wos_owner_bind(TaskHandle_t task, wos_slot_t slot) {
  if (!task) {
    return;
  }

  bool bound = false;
  taskENTER_CRITICAL(&s_lock);
  for (int i = 0; i < OWNER_TABLE_SIZE; i++) {
    if (s_owners[i].task == NULL || s_owners[i].task == task) {
      s_owners[i].task = task;
      s_owners[i].slot = slot;
      bound = true;
      break;
    }
  }
  taskEXIT_CRITICAL(&s_lock);

  if (!bound) {
    ESP_LOGE(TAG, "Owner table full; task defaults to main slot");
  }
}

void wos_owner_unbind(TaskHandle_t task) {
  if (!task) {
    return;
  }
  taskENTER_CRITICAL(&s_lock);
  for (int i = 0; i < OWNER_TABLE_SIZE; i++) {
    if (s_owners[i].task == task) {
      s_owners[i].task = NULL;
      break;
    }
  }
  taskEXIT_CRITICAL(&s_lock);
}

wos_slot_t wos_owner_current(void) {
  TaskHandle_t self = xTaskGetCurrentTaskHandle();
  wos_slot_t slot = WOS_SLOT_MAIN;

  taskENTER_CRITICAL(&s_lock);
  for (int i = 0; i < OWNER_TABLE_SIZE; i++) {
    if (s_owners[i].task == self) {
      slot = s_owners[i].slot;
      break;
    }
  }
  taskEXIT_CRITICAL(&s_lock);
  return slot;
}
