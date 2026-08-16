#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "wasm_export.h"

#include "common.h"
#include "modules.h"
#include "owner.h"
#include "priorities.h"

/*
 * Guest task spawning. Each spawned FreeRTOS task gets its own exec env via
 * wasm_runtime_spawn_exec_env (WAMR exec envs are not thread-safe, so the
 * app's env is never shared across tasks) and runs one guest function to
 * completion, then cleans itself up.
 *
 * App teardown terminates the instance (which traps all spawned envs), waits
 * for tasks to drain, and force-kills anything left via wos_tasks_reset().
 */

#define MAX_TASKS 8
#define MIN_TASK_STACK_BYTES 2048
#define MAX_TASK_STACK_BYTES (64 * 1024)

static const char* TAG = "wasm_task";

typedef struct {
  TaskHandle_t task;
  wasm_exec_env_t spawned_env;
  uint32_t table_idx;
  wos_slot_t owner;
  bool in_use;
} task_slot_t;

static task_slot_t s_tasks[MAX_TASKS];
static SemaphoreHandle_t s_lock;

static void task_trampoline(void* arg) {
  task_slot_t* slot = (task_slot_t*)arg;
  wos_owner_bind(xTaskGetCurrentTaskHandle(), slot->owner);

  uint32_t argv[1] = {0};
  if (!wasm_runtime_call_indirect(slot->spawned_env, slot->table_idx, 0, argv)) {
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(slot->spawned_env);
    ESP_LOGW(TAG, "Spawned task ended with: %s", wasm_runtime_get_exception(inst));
  }

  wasm_runtime_destroy_spawned_exec_env(slot->spawned_env);

  xSemaphoreTake(s_lock, portMAX_DELAY);
  slot->in_use = false;
  xSemaphoreGive(s_lock);

  wos_owner_unbind(xTaskGetCurrentTaskHandle());
  vTaskDelete(NULL);
}

/*
 * Run the guest function at `table_idx` (no arguments) on a new FreeRTOS
 * task. Returns WOS_OK or a negative error.
 */
static int32_t wasm_task_spawn(wasm_exec_env_t exec_env, uint32_t table_idx, char* name, int32_t stack_size,
                               int32_t priority, int32_t core) {
  if (stack_size < MIN_TASK_STACK_BYTES || stack_size > MAX_TASK_STACK_BYTES) {
    ESP_LOGE(TAG, "Task stack size %d out of range [%d, %d]", (int)stack_size, MIN_TASK_STACK_BYTES,
             MAX_TASK_STACK_BYTES);
    return WOS_ERR_INVALID_ARG;
  }
  /* Capped at the spawning app's own priority: a guest may never outrank its
   * manager or the serial handler. */
  wos_slot_t owner = wos_owner_current();
  int32_t guest_max = owner == WOS_SLOT_CHILD ? WOS_PRIO_GUEST_MAX_CHILD : WOS_PRIO_GUEST_MAX_MAIN;
  if (priority < 0 || priority > guest_max) {
    ESP_LOGE(TAG, "Task priority %d out of range [0, %d]", (int)priority, (int)guest_max);
    return WOS_ERR_INVALID_ARG;
  }
  if (core < -1 || core >= (int32_t)portNUM_PROCESSORS) {
    return WOS_ERR_INVALID_ARG;
  }

  xSemaphoreTake(s_lock, portMAX_DELAY);
  task_slot_t* slot = NULL;
  for (int i = 0; i < MAX_TASKS; i++) {
    if (!s_tasks[i].in_use) {
      slot = &s_tasks[i];
      slot->in_use = true;
      break;
    }
  }
  xSemaphoreGive(s_lock);

  if (!slot) {
    ESP_LOGE(TAG, "No free task slots (max %d)", MAX_TASKS);
    return WOS_ERR_NO_MEM;
  }

  slot->table_idx = table_idx;
  slot->owner = owner;
  slot->spawned_env = wasm_runtime_spawn_exec_env(exec_env);
  if (!slot->spawned_env) {
    ESP_LOGE(TAG, "Failed to spawn exec env (thread limit reached?)");
    slot->in_use = false;
    return WOS_ERR_INTERNAL;
  }

  BaseType_t created =
      xTaskCreatePinnedToCore(task_trampoline, name && name[0] ? name : "wasm_task", (uint32_t)stack_size, slot,
                              (UBaseType_t)priority, &slot->task, core < 0 ? tskNO_AFFINITY : core);
  if (created != pdPASS) {
    ESP_LOGE(TAG, "Failed to create task");
    wasm_runtime_destroy_spawned_exec_env(slot->spawned_env);
    slot->in_use = false;
    return WOS_ERR_NO_MEM;
  }
  return WOS_OK;
}

int wos_tasks_active(wos_slot_t owner) {
  if (!s_lock) {
    return 0;
  }
  int active = 0;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  for (int i = 0; i < MAX_TASKS; i++) {
    if (s_tasks[i].in_use && s_tasks[i].owner == owner) {
      active++;
    }
  }
  xSemaphoreGive(s_lock);
  return active;
}

void wos_tasks_reset(wos_slot_t owner) {
  if (!s_lock) {
    return;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  for (int i = 0; i < MAX_TASKS; i++) {
    if (s_tasks[i].in_use && s_tasks[i].owner == owner) {
      ESP_LOGE(TAG, "Force-killing spawned task that did not exit");
      vTaskDelete(s_tasks[i].task);
      wos_owner_unbind(s_tasks[i].task);
      wasm_runtime_destroy_spawned_exec_env(s_tasks[i].spawned_env);
      s_tasks[i].in_use = false;
    }
  }
  xSemaphoreGive(s_lock);
}

static NativeSymbol k_symbols[] = {
    {"task_spawn", wasm_task_spawn, "(i$iii)i", NULL},
};

bool wos_register_task(void) {
  if (!s_lock) {
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
      return false;
    }
  }
  return wasm_runtime_register_natives("task", k_symbols, sizeof(k_symbols) / sizeof(k_symbols[0]));
}
