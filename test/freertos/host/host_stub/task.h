/* Minimal FreeRTOS task.h stub for host tests */

#ifndef TASK_H
#define TASK_H

#include "FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *p_params);

/* Defined by the test: critical section depth, current task */
extern int g_crit;
extern TaskHandle_t g_cur;

#define taskENTER_CRITICAL()                (g_crit++)
#define taskEXIT_CRITICAL()                 (g_crit--)
#define taskENTER_CRITICAL_FROM_ISR()       ((UBaseType_t)(g_crit++))
#define taskEXIT_CRITICAL_FROM_ISR(mask)    ((void)(mask), g_crit--)
#define xTaskGetCurrentTaskHandle()         (g_cur)

TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
BaseType_t xTaskCreate(TaskFunction_t func, const char *name, uint16_t stack, void *p_params,
                       UBaseType_t priority, TaskHandle_t *p_handle);
void vTaskStartScheduler(void);

#endif /* TASK_H */
