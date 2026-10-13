/* uBroker POSIX test (white-box: includes ubroker.c directly)
 * Functional checks + multi-thread stress checks (mailbox topic and QUEUE topic). See docs/testing.md for how to run. */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#if !defined(UBK_CFG_CHECK_PUBLISHER_TASK) || (UBK_CFG_CHECK_PUBLISHER_TASK != 1)
#error "posix_test.c must be built with -DUBK_CFG_CHECK_PUBLISHER_TASK=1"
#endif

#include <stdio.h>
#include <pthread.h>
#include "ubroker.h"

#include "ubroker.c"

static int s_fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); s_fails++; } } while (0)
#define WS UBK_TOPIC_WEAPON_STATE

#define STRESS_TOPIC        UBK_TOPIC_SENSOR_IMU1
#define STRESS_PUBLISHES    (100000U)
#define STRESS_SUBSCRIBERS  (4U)

typedef struct
{
    uint32_t n_received;    /* NEW / NEW_LOST events */
    uint32_t n_counted;     /* sum of (1 + n_lost), first receive excluded */
    uint32_t n_torn;        /* messages whose fields disagree */
    uint32_t n_backwards;   /* sequence did not increase, or out of FIFO order */
    uint32_t first_value;
    uint32_t last_value;
} stress_result_t;

static ubk_pub_t s_stress_pub = UBK_PUB_INVALID;
static stress_result_t s_stress_result[STRESS_SUBSCRIBERS];

/* ---- Functional checks (same rules as the FreeRTOS host test) ---- */

static void *prv_wrong_thread(void *p_arg)
{
    weapon_state_t tx = WEAPON_STATE_INITIALIZER;
    ubk_pub_t *p_pub = (ubk_pub_t *)p_arg;

    CHECK(ubk_publish(WS, *p_pub, &tx, sizeof(tx)) == UBK_ERR_WRONG_TASK);
    return NULL;
}

static void prv_check_functional(void)
{
    weapon_state_t tx = WEAPON_STATE_INITIALIZER;
    weapon_state_t rx = WEAPON_STATE_INITIALIZER;
    sensor_imu_t imu = SENSOR_IMU_INITIALIZER;
    ubk_pub_t ws_pub = UBK_PUB_INVALID;
    ubk_pub_t pub2 = UBK_PUB_INVALID;
    ubk_sub_t sub = UBK_SUB_INITIALIZER;
    ubk_sub_t sub2 = UBK_SUB_INITIALIZER;
    ubk_topic_id_t bad_id = UBK_TOPIC_COUNT;
    pthread_t other;

    CHECK(UBK_TOPIC_INVALID == 0 && UBK_TOPIC_SENSOR_IMU0 == 1);
    CHECK(ubk_init_all() == UBK_OK);

    /* Invalid IDs */
    CHECK(ubk_advertise(UBK_TOPIC_INVALID, &pub2) == UBK_ERR_INVALID_TOPIC && pub2 == UBK_PUB_INVALID);
    CHECK(ubk_publish(bad_id, 0U, &tx, sizeof(tx)) == UBK_ERR_INVALID_TOPIC);
    ubk_subscribe(UBK_TOPIC_INVALID, &sub2, &rx, sizeof(rx));
    CHECK(sub2.event == UBK_EVT_ERR_TOPIC);

    /* Not published yet */
    ubk_subscribe(WS, &sub, &rx, sizeof(rx));
    CHECK(sub.event == UBK_EVT_NO_DATA);

    /* Publisher handle, size, wrong thread */
    CHECK(ubk_advertise(WS, &ws_pub) == UBK_OK && ws_pub != UBK_PUB_INVALID);
    CHECK(ubk_advertise(WS, &pub2) == UBK_ERR_ALREADY_ADVERTISED);
    CHECK(ubk_publish(WS, 0x1234U, &tx, sizeof(tx)) == UBK_ERR_INVALID_PUB);
    CHECK(ubk_publish(WS, ws_pub, &imu, sizeof(imu)) == UBK_ERR_SIZE);
    CHECK(pthread_create(&other, NULL, prv_wrong_thread, &ws_pub) == 0);
    CHECK(pthread_join(other, NULL) == 0);
    CHECK(s_topic_state[WS].has_data == false);

    /* Event flow */
    tx.arm_state = WEAPON_STATE_ARM_STATE_ARMED;
    CHECK(ubk_publish(WS, ws_pub, &tx, sizeof(tx)) == UBK_OK);
    CHECK(ubk_init_topic(WS) == UBK_ERR_ALREADY_PUBLISHED);
    ubk_subscribe(WS, &sub, &rx, sizeof(rx));
    CHECK(sub.event == UBK_EVT_NEW && sub.n_lost == 0U && rx.arm_state == 1U);
    ubk_subscribe(WS, &sub, &rx, sizeof(rx));
    CHECK(sub.event == UBK_EVT_NO_NEW);
    CHECK(ubk_publish(WS, ws_pub, &tx, sizeof(tx)) == UBK_OK);
    CHECK(ubk_publish(WS, ws_pub, &tx, sizeof(tx)) == UBK_OK);
    ubk_subscribe(WS, &sub, &rx, sizeof(rx));
    CHECK(sub.event == UBK_EVT_NEW_LOST && sub.n_lost == 1U);

    /* Wrap-around */
    s_topic_state[WS].seq = 0xFFFFFFFEU;
    sub.seq = 0xFFFFFFFEU;
    CHECK(ubk_publish(WS, ws_pub, &tx, sizeof(tx)) == UBK_OK);
    CHECK(ubk_publish(WS, ws_pub, &tx, sizeof(tx)) == UBK_OK);
    ubk_subscribe(WS, &sub, &rx, sizeof(rx));
    CHECK(sub.event == UBK_EVT_NEW_LOST && sub.n_lost == 1U && sub.seq == 0U);   /* 0 is a valid sequence */

    /* Errors */
    ubk_subscribe(WS, &sub, NULL, sizeof(rx));
    CHECK(sub.event == UBK_EVT_ERR_NULL);
    ubk_subscribe(WS, &sub, &imu, sizeof(imu));
    CHECK(sub.event == UBK_EVT_ERR_SIZE);
    ubk_subscribe(UBK_TOPIC_SENSOR_IMU0, &sub, &imu, sizeof(imu));
    CHECK(sub.event == UBK_EVT_ERR_TOPIC);
    ubk_subscribe(WS, NULL, &rx, sizeof(rx));   /* no-op, no crash */

    /* Stress topic is still untouched (sub2 stayed unbound after the invalid ID) */
    ubk_subscribe(STRESS_TOPIC, &sub2, &imu, sizeof(imu));
    CHECK(sub2.event == UBK_EVT_NO_DATA);
}

/* ---- Stress check ----
 * One publisher writes value k (1..N) into every checked field. A torn copy would mix two values. */

static uint32_t prv_value_of(const sensor_imu_t *p_imu, bool *p_is_consistent)
{
    uint32_t value = (uint32_t)p_imu->timestamp_us;

    *p_is_consistent = ((uint32_t)p_imu->accel.x == value) && ((uint32_t)p_imu->accel.y == value) &&
                       ((uint32_t)p_imu->gyro.z == value) && ((uint32_t)p_imu->temperature == value);
    return value;
}

static void *prv_stress_publisher(void *p_arg)
{
    sensor_imu_t imu = SENSOR_IMU_INITIALIZER;
    uint32_t k = 0U;

    (void)p_arg;
    CHECK(ubk_advertise(STRESS_TOPIC, &s_stress_pub) == UBK_OK);
    for (k = 1U; k <= STRESS_PUBLISHES; k++)
    {
        imu.timestamp_us = (uint64_t)k;
        imu.accel.x = (float)k;
        imu.accel.y = (float)k;
        imu.gyro.z = (float)k;
        imu.temperature = (float)k;
        CHECK(ubk_publish(STRESS_TOPIC, s_stress_pub, &imu, sizeof(imu)) == UBK_OK);
    }
    return NULL;
}

static void *prv_stress_subscriber(void *p_arg)
{
    stress_result_t *p_res = (stress_result_t *)p_arg;
    sensor_imu_t imu = SENSOR_IMU_INITIALIZER;
    ubk_sub_t sub = UBK_SUB_INITIALIZER;
    uint32_t prev_seq = 0U;
    uint32_t value = 0U;
    bool is_consistent = false;

    while (p_res->last_value < STRESS_PUBLISHES)
    {
        ubk_subscribe(STRESS_TOPIC, &sub, &imu, sizeof(imu));
        if ((sub.event == UBK_EVT_NEW) || (sub.event == UBK_EVT_NEW_LOST))
        {
            value = prv_value_of(&imu, &is_consistent);
            if (is_consistent == false)
            {
                p_res->n_torn++;
            }
            if ((p_res->n_received != 0U) && ((int32_t)(sub.seq - prev_seq) <= 0))
            {
                p_res->n_backwards++;
            }
            if (p_res->n_received == 0U)
            {
                p_res->first_value = value;
            }
            else
            {
                p_res->n_counted += 1U + sub.n_lost;
            }
            p_res->n_received++;
            p_res->last_value = value;
            prev_seq = sub.seq;
        }
    }
    return NULL;
}

/* Queue stress: a QUEUE topic must deliver in FIFO order, gaps only where n_lost says so */
static ubk_pub_t s_queue_pub = UBK_PUB_INVALID;
static stress_result_t s_queue_result[STRESS_SUBSCRIBERS];

static void *prv_queue_publisher(void *p_arg)
{
    weapon_cmd_t cmd = WEAPON_CMD_INITIALIZER;
    uint32_t k = 0U;

    (void)p_arg;
    CHECK(ubk_advertise(UBK_TOPIC_WEAPON_CMD, &s_queue_pub) == UBK_OK);
    for (k = 1U; k <= STRESS_PUBLISHES; k++)
    {
        cmd.req_id = k;
        CHECK(ubk_publish(UBK_TOPIC_WEAPON_CMD, s_queue_pub, &cmd, sizeof(cmd)) == UBK_OK);
    }
    return NULL;
}

static void *prv_queue_subscriber(void *p_arg)
{
    stress_result_t *p_res = (stress_result_t *)p_arg;
    weapon_cmd_t cmd = WEAPON_CMD_INITIALIZER;
    ubk_sub_t sub = UBK_SUB_INITIALIZER;

    while (p_res->last_value < STRESS_PUBLISHES)
    {
        ubk_subscribe(UBK_TOPIC_WEAPON_CMD, &sub, &cmd, sizeof(cmd));
        if ((sub.event == UBK_EVT_NEW) || (sub.event == UBK_EVT_NEW_LOST))
        {
            if ((p_res->n_received != 0U) && (cmd.req_id != (p_res->last_value + 1U + sub.n_lost)))
            {
                p_res->n_backwards++;               /* out of FIFO order */
            }
            if (p_res->n_received == 0U)
            {
                p_res->first_value = cmd.req_id;
            }
            else
            {
                p_res->n_counted += 1U + sub.n_lost;
            }
            p_res->n_received++;
            p_res->last_value = cmd.req_id;
        }
    }
    return NULL;
}

static void prv_check_queue_stress(void)
{
    pthread_t publisher;
    pthread_t subscriber[STRESS_SUBSCRIBERS];
    uint32_t i = 0U;

    for (i = 0U; i < STRESS_SUBSCRIBERS; i++)
    {
        CHECK(pthread_create(&subscriber[i], NULL, prv_queue_subscriber, &s_queue_result[i]) == 0);
    }
    CHECK(pthread_create(&publisher, NULL, prv_queue_publisher, NULL) == 0);
    CHECK(pthread_join(publisher, NULL) == 0);
    for (i = 0U; i < STRESS_SUBSCRIBERS; i++)
    {
        CHECK(pthread_join(subscriber[i], NULL) == 0);
        CHECK(s_queue_result[i].n_backwards == 0U);
        CHECK(s_queue_result[i].n_counted == s_queue_result[i].last_value - s_queue_result[i].first_value);
        printf("  queue subscriber %u: received %u of %u\n", (unsigned)i,
               (unsigned)s_queue_result[i].n_received, (unsigned)STRESS_PUBLISHES);
    }
}

static void prv_check_stress(void)
{
    pthread_t publisher;
    pthread_t subscriber[STRESS_SUBSCRIBERS];
    uint32_t i = 0U;

    for (i = 0U; i < STRESS_SUBSCRIBERS; i++)
    {
        CHECK(pthread_create(&subscriber[i], NULL, prv_stress_subscriber, &s_stress_result[i]) == 0);
    }
    CHECK(pthread_create(&publisher, NULL, prv_stress_publisher, NULL) == 0);
    CHECK(pthread_join(publisher, NULL) == 0);
    for (i = 0U; i < STRESS_SUBSCRIBERS; i++)
    {
        CHECK(pthread_join(subscriber[i], NULL) == 0);
        CHECK(s_stress_result[i].n_torn == 0U);
        CHECK(s_stress_result[i].n_backwards == 0U);
        /* every publish after the first receive is either received or counted as lost */
        CHECK(s_stress_result[i].n_counted == s_stress_result[i].last_value - s_stress_result[i].first_value);
        printf("  subscriber %u: received %u of %u\n", (unsigned)i,
               (unsigned)s_stress_result[i].n_received, (unsigned)STRESS_PUBLISHES);
    }
}

int main(void)
{
    prv_check_functional();
    prv_check_stress();
    prv_check_queue_stress();
    printf("%s (%d failures)\n", (s_fails != 0) ? "FAILED" : "ALL PASSED", s_fails);
    return s_fails;
}
