/* uBroker POSIX port implementation (fixed file, not generated)
 * Add to the build only when UBK_CFG_PORT == UBK_PORT_POSIX. */

#include "ubk_port.h"

#if (UBK_CFG_PORT == UBK_PORT_POSIX)

static pthread_mutex_t s_topic_lock[UBK_TOPIC_COUNT];   /* initialized once by prv_init_locks */
static pthread_once_t s_lock_once = PTHREAD_ONCE_INIT;
static bool s_is_lock_ready = false;

/* Runs once, on the first lock, so there is no dependency on ubk_init_all() order */
static void prv_init_locks(void)
{
    pthread_mutexattr_t attr;   /* opaque type, set by pthread_mutexattr_init */
    bool is_attr_ok = false;
    bool is_ok = false;
    uint32_t lock_index = 0U;

    is_attr_ok = (pthread_mutexattr_init(&attr) == 0);
    is_ok = is_attr_ok;
#if (UBK_CFG_POSIX_PRIO_INHERIT == 1)
    if (is_ok == true)
    {
        is_ok = (pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT) == 0);
    }
#endif
    for (lock_index = 0U; (is_ok == true) && (lock_index < (uint32_t)UBK_TOPIC_COUNT); lock_index++)
    {
        is_ok = (pthread_mutex_init(&s_topic_lock[lock_index], &attr) == 0);
    }
    if (is_attr_ok == true)
    {
        (void)pthread_mutexattr_destroy(&attr);
    }
    s_is_lock_ready = is_ok;
}

ubk_result_t ubk_port_lock(ubk_topic_id_t id)
{
    ubk_result_t result = UBK_ERR_OS;

    if ((pthread_once(&s_lock_once, prv_init_locks) == 0) && (s_is_lock_ready == true) &&
        ((uint32_t)id < (uint32_t)UBK_TOPIC_COUNT))
    {
        if (pthread_mutex_lock(&s_topic_lock[id]) == 0)
        {
            result = UBK_OK;
        }
    }
    return result;
}

void ubk_port_unlock(ubk_topic_id_t id)
{
    if ((s_is_lock_ready == true) && ((uint32_t)id < (uint32_t)UBK_TOPIC_COUNT))
    {
        (void)pthread_mutex_unlock(&s_topic_lock[id]);
    }
}

#endif /* UBK_CFG_PORT == UBK_PORT_POSIX */
