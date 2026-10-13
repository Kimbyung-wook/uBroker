/* uBroker common API implementation (fixed file, not generated) */

#include <string.h>
#include "ubroker.h"
#include "FreeRTOS.h"
#include "task.h"

/* Topic state (RAM) */
typedef struct
{
    uint32_t     seq;           /* publish sequence (0: not published) */
    bool         is_advertised;
#if (UBK_CFG_CHECK_PUBLISHER_TASK == 1)
    TaskHandle_t owner;
#endif
} ubk_topic_state_t;

#if (UBK_CFG_CHECK_PUBLISHER_TASK == 1)
#define UBK_TOPIC_STATE_INITIALIZER { 0U, false, NULL }
#else
#define UBK_TOPIC_STATE_INITIALIZER { 0U, false }
#endif

/* [0] is the UBK_TOPIC_INVALID slot (unused); remaining entries are zero-initialized */
static ubk_topic_state_t s_topic_state[UBK_TOPIC_COUNT] = { UBK_TOPIC_STATE_INITIALIZER };

/* ---- Common processing ---- */

/* 0 (UBK_TOPIC_INVALID) is the init value, so it is invalid */
static bool prv_is_valid_topic(ubk_topic_id_t id)
{
    return (((uint32_t)id > (uint32_t)UBK_TOPIC_INVALID) && ((uint32_t)id < (uint32_t)UBK_TOPIC_COUNT));
}

/* Call only inside a critical section */
static void prv_write_topic(ubk_topic_id_t id, const void *p_msg)
{
    if ((p_msg != NULL) && (prv_is_valid_topic(id) == true))
    {
        (void)memcpy(g_ubk_topic_meta[id].p_data, p_msg, g_ubk_topic_meta[id].size);
        s_topic_state[id].seq = s_topic_state[id].seq + 1U;   /* unsigned wrap-around is intended */
        if (s_topic_state[id].seq == 0U)
        {
            s_topic_state[id].seq = 1U;   /* 0 is reserved for not-published */
        }
    }
}

static ubk_result_t prv_check_publish(ubk_topic_id_t id, ubk_pub_t pub, const void *p_msg, size_t size)
{
    ubk_result_t result = UBK_OK;

    if (prv_is_valid_topic(id) == false)
    {
        result = UBK_ERR_INVALID_TOPIC;
    }
    else if (p_msg == NULL)
    {
        result = UBK_ERR_NULL_POINTER;
    }
    else if (size != g_ubk_topic_meta[id].size)
    {
        result = UBK_ERR_SIZE;
    }
    else if (pub != g_ubk_topic_meta[id].key)
    {
        result = UBK_ERR_INVALID_PUB;
    }
    else
    {
        result = UBK_OK;
    }
    return result;
}

ubk_result_t ubk_init_topic(ubk_topic_id_t id)
{
    ubk_result_t result = UBK_OK;

    if (prv_is_valid_topic(id) == false)
    {
        result = UBK_ERR_INVALID_TOPIC;
    }
    else
    {
        taskENTER_CRITICAL();
        if (s_topic_state[id].seq == 0U)
        {
            (void)memcpy(g_ubk_topic_meta[id].p_data, g_ubk_topic_meta[id].p_default, g_ubk_topic_meta[id].size);
        }
        else
        {
            result = UBK_ERR_ALREADY_PUBLISHED;
        }
        taskEXIT_CRITICAL();
    }
    return result;
}

ubk_result_t ubk_init_all(void)
{
    ubk_result_t result = UBK_OK;
    ubk_result_t topic_result = UBK_OK;
    uint32_t topic_index = 0U;

    for (topic_index = (uint32_t)UBK_TOPIC_INVALID + 1U; topic_index < (uint32_t)UBK_TOPIC_COUNT; topic_index++)
    {
        topic_result = ubk_init_topic((ubk_topic_id_t)topic_index);
        if ((topic_result != UBK_OK) && (result == UBK_OK))
        {
            result = topic_result;
        }
    }
    return result;
}

ubk_result_t ubk_advertise(ubk_topic_id_t id, ubk_pub_t *p_pub)
{
    ubk_result_t result = UBK_OK;

    if (p_pub == NULL)
    {
        result = UBK_ERR_NULL_POINTER;
    }
    else if (prv_is_valid_topic(id) == false)
    {
        *p_pub = UBK_PUB_INVALID;
        result = UBK_ERR_INVALID_TOPIC;
    }
    else
    {
        taskENTER_CRITICAL();
        if (s_topic_state[id].is_advertised == true)
        {
            result = UBK_ERR_ALREADY_ADVERTISED;
        }
        else
        {
            s_topic_state[id].is_advertised = true;
#if (UBK_CFG_CHECK_PUBLISHER_TASK == 1)
            s_topic_state[id].owner = xTaskGetCurrentTaskHandle();
#endif
        }
        taskEXIT_CRITICAL();

        if (result == UBK_OK)
        {
            *p_pub = g_ubk_topic_meta[id].key;
        }
        else
        {
            *p_pub = UBK_PUB_INVALID;
        }
    }
    return result;
}

ubk_result_t ubk_publish(ubk_topic_id_t id, ubk_pub_t pub, const void *p_msg, size_t size)
{
    ubk_result_t result = prv_check_publish(id, pub, p_msg, size);

#if (UBK_CFG_CHECK_PUBLISHER_TASK == 1)
    if ((result == UBK_OK) && (s_topic_state[id].owner != NULL) &&
        (xTaskGetCurrentTaskHandle() != s_topic_state[id].owner))
    {
        result = UBK_ERR_WRONG_TASK;
    }
#endif
    if (result == UBK_OK)
    {
        taskENTER_CRITICAL();
        prv_write_topic(id, p_msg);
        taskEXIT_CRITICAL();
    }
    return result;
}

ubk_result_t ubk_publish_from_isr(ubk_topic_id_t id, ubk_pub_t pub, const void *p_msg, size_t size)
{
    ubk_result_t result = prv_check_publish(id, pub, p_msg, size);
    UBaseType_t saved_mask = 0U;

    if (result == UBK_OK)
    {
        saved_mask = taskENTER_CRITICAL_FROM_ISR();
        prv_write_topic(id, p_msg);
        taskEXIT_CRITICAL_FROM_ISR(saved_mask);
    }
    return result;
}

void ubk_subscribe(ubk_topic_id_t id, ubk_sub_t *p_sub, void *p_msg, size_t size)
{
    uint32_t prev_seq = 0U;
    uint32_t n_diff = 0U;
    bool is_copied = false;

    if (p_sub != NULL)
    {
        p_sub->n_lost = 0U;
        if (prv_is_valid_topic(id) == false)
        {
            p_sub->event = UBK_EVT_ERR_TOPIC;
        }
        else if (p_msg == NULL)
        {
            p_sub->event = UBK_EVT_ERR_NULL;
        }
        else if (size != g_ubk_topic_meta[id].size)
        {
            p_sub->event = UBK_EVT_ERR_SIZE;
        }
        else if ((p_sub->topic != UBK_PUB_INVALID) && (p_sub->topic != g_ubk_topic_meta[id].key))
        {
            p_sub->event = UBK_EVT_ERR_TOPIC;
        }
        else
        {
            p_sub->topic = g_ubk_topic_meta[id].key;
            prev_seq = p_sub->seq;

            taskENTER_CRITICAL();
            if (s_topic_state[id].seq == 0U)
            {
                p_sub->event = UBK_EVT_NO_DATA;
            }
            else if (s_topic_state[id].seq == prev_seq)
            {
                p_sub->event = UBK_EVT_NO_NEW;
            }
            else
            {
                (void)memcpy(p_msg, g_ubk_topic_meta[id].p_data, size);
                p_sub->seq = s_topic_state[id].seq;
                is_copied = true;
            }
            taskEXIT_CRITICAL();

            if (is_copied == true)
            {
                if (prev_seq != 0U)
                {
                    n_diff = p_sub->seq - prev_seq;
                    if (p_sub->seq < prev_seq)
                    {
                        n_diff = n_diff - 1U;   /* compensate for the skipped 0 on wrap-around */
                    }
                    p_sub->n_lost = n_diff - 1U;
                }

                if (p_sub->n_lost > 0U)
                {
                    p_sub->event = UBK_EVT_NEW_LOST;
                }
                else
                {
                    p_sub->event = UBK_EVT_NEW;
                }
            }
        }
    }
}
