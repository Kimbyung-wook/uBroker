/* uBroker common types (fixed file, not generated) */

#ifndef UBK_TYPES_H
#define UBK_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1: reject publish from any task other than the advertiser (requires INCLUDE_xTaskGetCurrentTaskHandle) */
#ifndef UBK_CFG_CHECK_PUBLISHER_TASK
#define UBK_CFG_CHECK_PUBLISHER_TASK (0)
#endif

/* Publisher handle (obtainable only via advertise) */
typedef uint32_t ubk_pub_t;
#define UBK_PUB_INVALID ((ubk_pub_t)0U)

/* API result codes */
typedef enum
{
    UBK_OK = 0,
    UBK_ERR_NULL_POINTER,
    UBK_ERR_INVALID_TOPIC,
    UBK_ERR_SIZE,
    UBK_ERR_ALREADY_ADVERTISED,
    UBK_ERR_INVALID_PUB,
    UBK_ERR_WRONG_TASK,
    UBK_ERR_ALREADY_PUBLISHED
} ubk_result_t;

/* subscribe result (ubk_sub_t.event) */
typedef enum
{
    UBK_EVT_NONE = 0,       /* subscribe not called yet */
    UBK_EVT_NO_DATA,        /* topic not published yet */
    UBK_EVT_NO_NEW,         /* no new message, previous value kept */
    UBK_EVT_NEW,            /* new message copied */
    UBK_EVT_NEW_LOST,       /* new message copied, n_lost messages missed */
    UBK_EVT_ERR_NULL,       /* p_msg is NULL */
    UBK_EVT_ERR_SIZE,       /* size differs from the topic message size */
    UBK_EVT_ERR_TOPIC       /* invalid topic ID, or cursor bound to another topic */
} ubk_event_t;

/* Subscription cursor: one per subscriber, bound to a topic on first subscribe */
typedef struct
{
    ubk_pub_t   topic;      /* bound topic key (UBK_PUB_INVALID: unbound) */
    uint32_t    seq;        /* last received sequence (0: none received) */
    uint32_t    n_lost;     /* messages missed in this call */
    ubk_event_t event;      /* result of this call */
} ubk_sub_t;

#define UBK_SUB_INITIALIZER { UBK_PUB_INVALID, 0U, 0U, UBK_EVT_NONE }

/* Topic info (defined by generated ubk_msgs.c, used by ubroker.c) */
typedef struct
{
    void        *p_data;        /* shared memory */
    const void  *p_default;     /* default value */
    size_t      size;           /* message size */
    ubk_pub_t   key;            /* topic key = publisher handle value */
} ubk_topic_meta_t;

#ifdef __cplusplus
}
#endif

#endif /* UBK_TYPES_H */
