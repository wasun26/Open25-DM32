/* SPDX-License-Identifier: MIT */
/* Host test double only. */
typedef void *TaskHandle_t;
#define taskSCHEDULER_RUNNING 1
int xTaskGetSchedulerState(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
const char *pcTaskGetName(TaskHandle_t);
