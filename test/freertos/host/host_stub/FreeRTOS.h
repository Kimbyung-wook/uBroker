/* Minimal FreeRTOS stub for host tests */

#ifndef FREERTOS_H
#define FREERTOS_H

#include <stdint.h>

typedef uint32_t TickType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t StackType_t;
typedef uint32_t EventBits_t;

#define configASSERT(x)     ((void)(x))
#define pdMS_TO_TICKS(ms)   ((TickType_t)(ms))

#endif /* FREERTOS_H */
