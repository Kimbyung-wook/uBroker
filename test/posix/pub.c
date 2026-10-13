/* uBroker publish example for POSIX (thread publish, signal -> thread publish)
 * Build: -pthread -DUBK_CFG_PORT=UBK_PORT_POSIX, generated with ubroker_gen.py --os posix */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include "ubroker.h"

#define WEAPON_PERIOD_NS    (20000000L)     /* publish period, 50 Hz */
#define IMU_EVENT_PERIOD_NS (10000000L)     /* simulated IMU event, 100 Hz */
#define NS_PER_SEC          (1000000000L)

void *SensorFusionThread(void *p_arg);      /* sub.c */

/* A signal handler must not call ubk_publish (mutex is not async-signal-safe).
 * It only wakes the IMU thread, which publishes. */
static sem_t s_imu_ready;
static ubk_pub_t s_imu0_pub = UBK_PUB_INVALID;

static void prv_add_ns(struct timespec *p_ts, long ns)
{
    p_ts->tv_nsec += ns;
    while (p_ts->tv_nsec >= NS_PER_SEC)
    {
        p_ts->tv_nsec -= NS_PER_SEC;
        p_ts->tv_sec += 1;
    }
}

static void prv_on_imu_signal(int sig)
{
    (void)sig;
    (void)sem_post(&s_imu_ready);           /* async-signal-safe */
}

static void *WeaponControlThread(void *p_arg)
{
    ubk_pub_t pub = UBK_PUB_INVALID;
    weapon_state_t state = WEAPON_STATE_INITIALIZER;
    struct timespec next = { 0, 0 };
    uint32_t tick = 0U;

    (void)p_arg;

    /* 1. Obtain publish rights (only one publisher per topic succeeds) */
    if (ubk_advertise(UBK_TOPIC_WEAPON_STATE, &pub) == UBK_OK)
    {
        (void)clock_gettime(CLOCK_MONOTONIC, &next);
        for (;;)
        {
            /* 2. Update fields */
            tick++;
            state.timestamp_us = (uint64_t)next.tv_sec * 1000000ULL + (uint64_t)next.tv_nsec / 1000ULL;
            state.target_x = 1.0F;
            state.target_y = 2.0F;
            state.arm_state = WEAPON_STATE_ARM_STATE_ARMED;
            state.is_locked = true;
            state.last_change_tick = tick;

            /* 3. Publish */
            (void)ubk_publish(UBK_TOPIC_WEAPON_STATE, pub, &state, sizeof(state));

            /* 4. Wait for the next period (absolute time, no drift) */
            prv_add_ns(&next, WEAPON_PERIOD_NS);
            (void)clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        }
    }
    return NULL;
}

static void *ImuThread(void *p_arg)
{
    sensor_imu_t imu = SENSOR_IMU_INITIALIZER;

    (void)p_arg;

    /* The publisher is the thread that advertises */
    if (ubk_advertise(UBK_TOPIC_SENSOR_IMU0, &s_imu0_pub) == UBK_OK)
    {
        for (;;)
        {
            if (sem_wait(&s_imu_ready) == 0)
            {
                imu.accel.z = 9.81F;        /* replace with real sensor values */
                imu.is_calibrated = 1U;
                (void)ubk_publish(UBK_TOPIC_SENSOR_IMU0, s_imu0_pub, &imu, sizeof(imu));
            }
        }
    }
    return NULL;
}

int main(void)
{
    pthread_t weapon_thread;
    pthread_t imu_thread;
    pthread_t fusion_thread;
    struct sigaction action;
    struct timespec next = { 0, 0 };

    (void)memset(&action, 0, sizeof(action));

    /* 1. Init shared memory of all topics (once, before publishing) */
    (void)ubk_init_all();

    /* 2. Signal -> semaphore -> IMU thread */
    (void)sem_init(&s_imu_ready, 0, 0U);
    (void)sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    action.sa_handler = prv_on_imu_signal;
    (void)sigaction(SIGUSR1, &action, NULL);

    /* 3. Create threads */
    (void)pthread_create(&weapon_thread, NULL, WeaponControlThread, NULL);
    (void)pthread_create(&imu_thread, NULL, ImuThread, NULL);
    (void)pthread_create(&fusion_thread, NULL, SensorFusionThread, NULL);

    /* 4. Simulate an IMU interrupt with a periodic signal */
    (void)clock_gettime(CLOCK_MONOTONIC, &next);
    for (;;)
    {
        prv_add_ns(&next, IMU_EVENT_PERIOD_NS);
        (void)clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        (void)raise(SIGUSR1);
    }
}
