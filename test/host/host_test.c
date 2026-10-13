/* uBroker host test (white-box: includes ubroker.c directly)
 * See README.md for how to run */

#if !defined(UBK_CFG_CHECK_PUBLISHER_TASK) || (UBK_CFG_CHECK_PUBLISHER_TASK != 1)
#error "host_test.c must be built with -DUBK_CFG_CHECK_PUBLISHER_TASK=1"
#endif

#include <stdio.h>
#include "ubroker.h"
#include "task.h"

int g_crit = 0;
TaskHandle_t g_cur = (TaskHandle_t)1;

#include "ubroker.c"

static int s_fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); s_fails++; } } while (0)
#define WS UBK_TOPIC_WEAPON_STATE

int main(void)
{
    weapon_state_t tx = WEAPON_STATE_INITIALIZER;
    weapon_state_t rx = WEAPON_STATE_INITIALIZER;
    sensor_imu_t imu = SENSOR_IMU_INITIALIZER;
    ubk_pub_t ws_pub = UBK_PUB_INVALID;
    ubk_pub_t pub2 = UBK_PUB_INVALID;
    ubk_pub_t imu_pub = UBK_PUB_INVALID;
    ubk_sub_t sub = UBK_SUB_INITIALIZER;
    ubk_sub_t sub2 = UBK_SUB_INITIALIZER;
    ubk_topic_id_t bad_id = UBK_TOPIC_COUNT;

    /* Topic IDs: 0 is invalid, TYPE_ONLY (vector3) has no topic */
    CHECK(UBK_TOPIC_INVALID == 0 && UBK_TOPIC_SENSOR_IMU0 == 1);
    CHECK(UBK_TOPIC_COUNT == 4);

    /* Init */
    CHECK(imu.temperature > 24.9f && imu.temperature < 25.1f);
    CHECK(ubk_init_all() == UBK_OK);
    CHECK(((const sensor_imu_t *)g_ubk_topic_meta[UBK_TOPIC_SENSOR_IMU1].p_data)->temperature > 24.9f);

    /* Invalid ID 0 */
    CHECK(ubk_advertise(UBK_TOPIC_INVALID, &pub2) == UBK_ERR_INVALID_TOPIC && pub2 == UBK_PUB_INVALID);
    CHECK(ubk_init_topic(UBK_TOPIC_INVALID) == UBK_ERR_INVALID_TOPIC);
    CHECK(ubk_publish(UBK_TOPIC_INVALID, 0U, &tx, sizeof(tx)) == UBK_ERR_INVALID_TOPIC);
    ubk_subscribe(UBK_TOPIC_INVALID, &sub2, &rx, sizeof(rx));
    CHECK(sub2.event == UBK_EVT_ERR_TOPIC && sub2.topic == UBK_PUB_INVALID);

    /* Not published yet */
    ubk_subscribe(WS, &sub, &rx, sizeof(rx));
    CHECK(sub.event == UBK_EVT_NO_DATA && sub.topic == g_ubk_topic_meta[WS].key && sub.seq == 0U);

    /* Publisher handle / publisher restriction / invalid topic / size */
    CHECK(ubk_publish(WS, 0x1234U, &tx, sizeof(tx)) == UBK_ERR_INVALID_PUB);
    CHECK(ubk_advertise(WS, NULL) == UBK_ERR_NULL_POINTER);
    CHECK(ubk_advertise(bad_id, &pub2) == UBK_ERR_INVALID_TOPIC && pub2 == UBK_PUB_INVALID);
    CHECK(ubk_advertise(WS, &ws_pub) == UBK_OK && ws_pub != UBK_PUB_INVALID);
    CHECK(ubk_advertise(WS, &pub2) == UBK_ERR_ALREADY_ADVERTISED && pub2 == UBK_PUB_INVALID);
    CHECK(ubk_publish(UBK_TOPIC_SENSOR_IMU0, ws_pub, &imu, sizeof(imu)) == UBK_ERR_INVALID_PUB);
    CHECK(ubk_publish(bad_id, ws_pub, &tx, sizeof(tx)) == UBK_ERR_INVALID_TOPIC);
    CHECK(ubk_publish(WS, ws_pub, NULL, sizeof(tx)) == UBK_ERR_NULL_POINTER);
    CHECK(ubk_publish(WS, ws_pub, &imu, sizeof(imu)) == UBK_ERR_SIZE);
    CHECK(ubk_init_topic(bad_id) == UBK_ERR_INVALID_TOPIC);
    g_cur = (TaskHandle_t)2;
    CHECK(ubk_publish(WS, ws_pub, &tx, sizeof(tx)) == UBK_ERR_WRONG_TASK);
    g_cur = (TaskHandle_t)1;
    CHECK(s_topic_state[WS].seq == 0U);

    /* First receive */
    tx.arm_state = WEAPON_STATE_ARM_STATE_ARMED;
    tx.target_x = 1.5f;
    CHECK(ubk_publish(WS, ws_pub, &tx, sizeof(tx)) == UBK_OK);
    CHECK(ubk_init_topic(WS) == UBK_ERR_ALREADY_PUBLISHED);
    ubk_subscribe(WS, &sub, &rx, sizeof(rx));
    CHECK(sub.event == UBK_EVT_NEW && sub.n_lost == 0U && sub.seq == 1U && rx.arm_state == 1U);

    /* No new message: rx kept */
    rx.target_x = 9.0f;
    ubk_subscribe(WS, &sub, &rx, sizeof(rx));
    CHECK(sub.event == UBK_EVT_NO_NEW && rx.target_x > 8.9f);

    /* Two lost */
    CHECK(ubk_publish(WS, ws_pub, &tx, sizeof(tx)) == UBK_OK);
    CHECK(ubk_publish_from_isr(WS, ws_pub, &tx, sizeof(tx)) == UBK_OK);
    CHECK(ubk_publish(WS, ws_pub, &tx, sizeof(tx)) == UBK_OK);
    ubk_subscribe(WS, &sub, &rx, sizeof(rx));
    CHECK(sub.event == UBK_EVT_NEW_LOST && sub.n_lost == 2U && sub.seq == 4U && rx.target_x < 1.6f);
    ubk_subscribe(WS, &sub, &rx, sizeof(rx));
    CHECK(sub.event == UBK_EVT_NO_NEW && sub.n_lost == 0U);

    /* Second subscriber: independent (reuses the cursor left unbound by the invalid ID) */
    ubk_subscribe(WS, &sub2, &rx, sizeof(rx));
    CHECK(sub2.event == UBK_EVT_NEW && sub2.n_lost == 0U && sub2.seq == 4U);

    /* Sequence wrap-around (0 skipped) */
    s_topic_state[WS].seq = 0xFFFFFFFEU;
    sub.seq = 0xFFFFFFFEU;
    CHECK(ubk_publish(WS, ws_pub, &tx, sizeof(tx)) == UBK_OK);
    CHECK(ubk_publish(WS, ws_pub, &tx, sizeof(tx)) == UBK_OK);
    CHECK(s_topic_state[WS].seq == 1U);
    ubk_subscribe(WS, &sub, &rx, sizeof(rx));
    CHECK(sub.event == UBK_EVT_NEW_LOST && sub.n_lost == 1U && sub.seq == 1U);

    /* Receive errors */
    ubk_subscribe(WS, &sub, NULL, sizeof(rx));
    CHECK(sub.event == UBK_EVT_ERR_NULL && sub.seq == 1U);
    ubk_subscribe(WS, &sub, &imu, sizeof(imu));
    CHECK(sub.event == UBK_EVT_ERR_SIZE && sub.seq == 1U);
    ubk_subscribe(bad_id, &sub, &rx, sizeof(rx));
    CHECK(sub.event == UBK_EVT_ERR_TOPIC);
    ubk_subscribe(WS, NULL, &rx, sizeof(rx));   /* no-op, no crash */
    ubk_subscribe(UBK_TOPIC_SENSOR_IMU0, &sub, &imu, sizeof(imu));   /* weapon_state cursor used on imu0 */
    CHECK(sub.event == UBK_EVT_ERR_TOPIC && sub.topic == g_ubk_topic_meta[WS].key && sub.seq == 1U);

    /* Multi-instance: publishing imu0 does not affect imu1 */
    {
        ubk_sub_t s0 = UBK_SUB_INITIALIZER;
        ubk_sub_t s1 = UBK_SUB_INITIALIZER;

        CHECK(ubk_advertise(UBK_TOPIC_SENSOR_IMU0, &imu_pub) == UBK_OK);
        imu.accel.z = 9.81f;
        CHECK(ubk_publish(UBK_TOPIC_SENSOR_IMU0, imu_pub, &imu, sizeof(imu)) == UBK_OK);
        imu.accel.z = 0.0f;
        ubk_subscribe(UBK_TOPIC_SENSOR_IMU0, &s0, &imu, sizeof(imu));
        CHECK(s0.event == UBK_EVT_NEW && imu.accel.z > 9.8f);
        ubk_subscribe(UBK_TOPIC_SENSOR_IMU1, &s1, &imu, sizeof(imu));
        CHECK(s1.event == UBK_EVT_NO_DATA);
    }

    /* Critical section balance, pack(1) sizes */
    CHECK(g_crit == 0);
    CHECK(sizeof(weapon_state_t) == 8 + 4 + 4 + 1 + 1 + 4);
    CHECK(sizeof(vector3_t) == 12);
    CHECK(sizeof(sensor_imu_t) == 8 + 12 + 12 + 4 + 1);

    printf("%s (%d failures)\n", (s_fails != 0) ? "FAILED" : "ALL PASSED", s_fails);
    return s_fails;
}
