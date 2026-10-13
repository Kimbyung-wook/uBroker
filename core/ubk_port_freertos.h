/* uBroker FreeRTOS port (fixed file, not generated)
 * Lock = critical section (interrupts masked), so it never fails. */

#ifndef UBK_PORT_FREERTOS_H
#define UBK_PORT_FREERTOS_H

#include "FreeRTOS.h"
#include "task.h"

typedef TaskHandle_t ubk_port_owner_t;

static inline ubk_result_t ubk_port_lock(ubk_topic_id_t id)
{
    (void)id;
    taskENTER_CRITICAL();
    return UBK_OK;
}

static inline void ubk_port_unlock(ubk_topic_id_t id)
{
    (void)id;
    taskEXIT_CRITICAL();
}

static inline UBaseType_t ubk_port_lock_from_isr(void)
{
    return taskENTER_CRITICAL_FROM_ISR();
}

static inline void ubk_port_unlock_from_isr(UBaseType_t mask)
{
    taskEXIT_CRITICAL_FROM_ISR(mask);
}

#if (UBK_CFG_CHECK_PUBLISHER_TASK == 1)
/* Before the scheduler starts there is no current task (ISR publishers advertise there) */
static inline bool ubk_port_get_self(ubk_port_owner_t *p_owner)
{
    *p_owner = xTaskGetCurrentTaskHandle();
    return (*p_owner != NULL);
}

static inline bool ubk_port_is_self(ubk_port_owner_t owner)
{
    return (xTaskGetCurrentTaskHandle() == owner);
}
#endif

#endif /* UBK_PORT_FREERTOS_H */
