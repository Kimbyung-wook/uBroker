/* uBroker common API implementation (fixed file, not generated) */

#include <string.h>
#include "ubroker.h"
#include "ubk_port.h"

/* Generated code (ubk_msgs.h) must target the same OS as this build */
#if !defined(UBK_GEN_OS) || (UBK_GEN_OS != UBK_CFG_PORT)
#error "ubk_msgs.h was generated for a different OS: match ubroker_gen.py --os with UBK_CFG_PORT"
#endif

/* Topic state (RAM) */
typedef struct
{
    uint32_t         seq;           /* sequence of the last published message (meaningful only if has_data) */
    uint32_t         n_stored;      /* messages held in the queue (1 ~ queue_len once published) */
    bool             has_data;      /* false: not published yet */
    bool             is_advertised;
#if (UBK_CFG_CHECK_PUBLISHER_TASK == 1)
    bool             has_owner;     /* false: advertised with no current task (e.g. before the scheduler) */
    ubk_port_owner_t owner;
#endif
} ubk_topic_state_t;

/* All members zero/false: not published, not advertised, no owner */
#define UBK_TOPIC_STATE_INITIALIZER { .seq = 0U }

/* [0] is the UBK_TOPIC_INVALID slot (unused); remaining entries are zero-initialized */
static ubk_topic_state_t s_topic_state[UBK_TOPIC_COUNT] = { UBK_TOPIC_STATE_INITIALIZER };

/* ---- Common processing ---- */

/* 0 (UBK_TOPIC_INVALID) is the init value, so it is invalid */
static bool prv_is_valid_topic(ubk_topic_id_t id)
{
    return (((uint32_t)id > (uint32_t)UBK_TOPIC_INVALID) && ((uint32_t)id < (uint32_t)UBK_TOPIC_COUNT));
}

/* Address of the slot holding sequence seq. queue_len is a power of 2, so the slot order
 * stays continuous when seq wraps around (2^32 is a multiple of queue_len) */
static uint8_t *prv_slot(ubk_topic_id_t id, uint32_t seq)
{
    uint32_t slot_index = seq & (g_ubk_topic_meta[id].queue_len - 1U);

    return &((uint8_t *)g_ubk_topic_meta[id].p_data)[(size_t)slot_index * g_ubk_topic_meta[id].size];
}

/* Call only while the topic is locked */
static void prv_write_topic(ubk_topic_id_t id, const void *p_msg)
{
    ubk_topic_state_t *p_state = NULL;

    if ((p_msg != NULL) && (prv_is_valid_topic(id) == true))
    {
        p_state = &s_topic_state[id];
        if (p_state->has_data == true)
        {
            p_state->seq = p_state->seq + 1U;   /* unsigned wrap-around is intended */
        }
        else
        {
            p_state->seq = 0U;                  /* first message */
            p_state->has_data = true;
        }
        if (p_state->n_stored < g_ubk_topic_meta[id].queue_len)
        {
            p_state->n_stored = p_state->n_stored + 1U;
        }
        (void)memcpy(prv_slot(id, p_state->seq), p_msg, g_ubk_topic_meta[id].size);
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
    uint32_t slot_seq = 0U;

    if (prv_is_valid_topic(id) == false)
    {
        result = UBK_ERR_INVALID_TOPIC;
    }
    else
    {
        result = ubk_port_lock(id);
        if (result == UBK_OK)
        {
            if (s_topic_state[id].has_data == false)
            {
                for (slot_seq = 0U; slot_seq < g_ubk_topic_meta[id].queue_len; slot_seq++)
                {
                    (void)memcpy(prv_slot(id, slot_seq), g_ubk_topic_meta[id].p_default, g_ubk_topic_meta[id].size);
                }
            }
            else
            {
                result = UBK_ERR_ALREADY_PUBLISHED;
            }
            ubk_port_unlock(id);
        }
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
        result = ubk_port_lock(id);
        if (result == UBK_OK)
        {
            if (s_topic_state[id].is_advertised == true)
            {
                result = UBK_ERR_ALREADY_ADVERTISED;
            }
            else
            {
                s_topic_state[id].is_advertised = true;
#if (UBK_CFG_CHECK_PUBLISHER_TASK == 1)
                s_topic_state[id].has_owner = ubk_port_get_self(&s_topic_state[id].owner);
#endif
            }
            ubk_port_unlock(id);
        }

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
    if ((result == UBK_OK) && (s_topic_state[id].has_owner == true) &&
        (ubk_port_is_self(s_topic_state[id].owner) == false))
    {
        result = UBK_ERR_WRONG_TASK;
    }
#endif
    if (result == UBK_OK)
    {
        result = ubk_port_lock(id);
        if (result == UBK_OK)
        {
            prv_write_topic(id, p_msg);
            ubk_port_unlock(id);
        }
    }
    return result;
}

#if (UBK_CFG_PORT == UBK_PORT_FREERTOS)
ubk_result_t ubk_publish_from_isr(ubk_topic_id_t id, ubk_pub_t pub, const void *p_msg, size_t size)
{
    ubk_result_t result = prv_check_publish(id, pub, p_msg, size);
    UBaseType_t saved_mask = 0U;

    if (result == UBK_OK)
    {
        saved_mask = ubk_port_lock_from_isr();
        prv_write_topic(id, p_msg);
        ubk_port_unlock_from_isr(saved_mask);
    }
    return result;
}
#endif

/* Pick the next message for the cursor (oldest first). Call only while the topic is locked */
static void prv_read_topic(ubk_topic_id_t id, ubk_sub_t *p_sub, void *p_msg)
{
    const ubk_topic_state_t *p_state = &s_topic_state[id];
    uint32_t oldest_seq = 0U;
    uint32_t next_seq = 0U;
    uint32_t n_behind = 0U;

    if ((p_sub != NULL) && (p_msg != NULL))
    {
        if (p_state->has_data == false)
        {
            p_sub->event = UBK_EVT_NO_DATA;
        }
        else if ((p_sub->is_valid == true) && (p_sub->seq == p_state->seq))
        {
            p_sub->event = UBK_EVT_NO_NEW;
        }
        else
        {
            oldest_seq = p_state->seq - (p_state->n_stored - 1U);   /* unsigned wrap-around is intended */
            if (p_sub->is_valid == false)
            {
                next_seq = oldest_seq;                              /* first read: start from the oldest */
            }
            else
            {
                n_behind = p_state->seq - p_sub->seq;               /* 1 or more */
                if (n_behind > p_state->n_stored)
                {
                    p_sub->n_lost = n_behind - p_state->n_stored;   /* overwritten before being read */
                    next_seq = oldest_seq;
                }
                else
                {
                    next_seq = p_sub->seq + 1U;
                }
            }
            (void)memcpy(p_msg, prv_slot(id, next_seq), g_ubk_topic_meta[id].size);
            p_sub->seq = next_seq;
            p_sub->is_valid = true;
            p_sub->n_pending = p_state->seq - next_seq;
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

void ubk_subscribe(ubk_topic_id_t id, ubk_sub_t *p_sub, void *p_msg, size_t size)
{
    if (p_sub != NULL)
    {
        p_sub->n_lost = 0U;
        p_sub->n_pending = 0U;
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
        else if (ubk_port_lock(id) != UBK_OK)
        {
            p_sub->event = UBK_EVT_ERR_OS;
        }
        else
        {
            p_sub->topic = g_ubk_topic_meta[id].key;
            prv_read_topic(id, p_sub, p_msg);
            ubk_port_unlock(id);
        }
    }
}
