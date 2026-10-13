/* uBroker POSIX port (fixed file, not generated)
 * Lock = one pthread mutex per topic (ubk_port_posix.c). Build with -pthread. */

#ifndef UBK_PORT_POSIX_H
#define UBK_PORT_POSIX_H

#include <pthread.h>

typedef pthread_t ubk_port_owner_t;

ubk_result_t ubk_port_lock(ubk_topic_id_t id);
void ubk_port_unlock(ubk_topic_id_t id);

static inline bool ubk_port_get_self(ubk_port_owner_t *p_owner)
{
    *p_owner = pthread_self();
    return true;
}

static inline bool ubk_port_is_self(ubk_port_owner_t owner)
{
    return (pthread_equal(pthread_self(), owner) != 0);
}

#endif /* UBK_PORT_POSIX_H */
