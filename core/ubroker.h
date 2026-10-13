/* uBroker common API (fixed file, not generated) */

#ifndef UBROKER_H
#define UBROKER_H

#include "ubk_types.h"
#include "ubk_msgs.h"   /* generated: message types, topic IDs */

#ifdef __cplusplus
extern "C" {
#endif

/* Pass sizeof(message variable) as size (rejected if it differs from the topic message size) */
ubk_result_t ubk_init_all(void);
ubk_result_t ubk_init_topic(ubk_topic_id_t id);
ubk_result_t ubk_advertise(ubk_topic_id_t id, ubk_pub_t *p_pub);
ubk_result_t ubk_publish(ubk_topic_id_t id, ubk_pub_t pub, const void *p_msg, size_t size);
ubk_result_t ubk_publish_from_isr(ubk_topic_id_t id, ubk_pub_t pub, const void *p_msg, size_t size);
void ubk_subscribe(ubk_topic_id_t id, ubk_sub_t *p_sub, void *p_msg, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* UBROKER_H */
