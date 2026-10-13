/* uBroker OS port interface (fixed file, not generated)
 *
 * Every port provides:
 *   ubk_port_owner_t
 *   ubk_result_t ubk_port_lock(ubk_topic_id_t id)      UBK_OK or UBK_ERR_OS
 *   void         ubk_port_unlock(ubk_topic_id_t id)
 *   bool         ubk_port_get_self(ubk_port_owner_t *p_owner)   false: no current task/thread
 *   bool         ubk_port_is_self(ubk_port_owner_t owner)
 * FreeRTOS only:
 *   UBaseType_t  ubk_port_lock_from_isr(void)
 *   void         ubk_port_unlock_from_isr(UBaseType_t mask)
 */

#ifndef UBK_PORT_H
#define UBK_PORT_H

#include "ubroker.h"

#if (UBK_CFG_PORT == UBK_PORT_FREERTOS)
#include "ubk_port_freertos.h"
#elif (UBK_CFG_PORT == UBK_PORT_POSIX)
#include "ubk_port_posix.h"
#else
#error "UBK_CFG_PORT must be UBK_PORT_FREERTOS or UBK_PORT_POSIX"
#endif

#endif /* UBK_PORT_H */
