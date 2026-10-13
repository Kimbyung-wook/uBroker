/* uBroker subscribe example for POSIX */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <time.h>
#include "ubroker.h"

#define FUSION_PERIOD_NS    (10000000L)     /* 100 Hz */
#define NS_PER_SEC          (1000000000L)

void *SensorFusionThread(void *p_arg)
{
    /* 1. Declare message variables and one cursor per topic (sharing a cursor gives UBK_EVT_ERR_TOPIC) */
    weapon_state_t  current_weapon = WEAPON_STATE_INITIALIZER;
    sensor_imu_t    current_imu0   = SENSOR_IMU_INITIALIZER;
    ubk_sub_t       weapon_sub     = UBK_SUB_INITIALIZER;
    ubk_sub_t       imu0_sub       = UBK_SUB_INITIALIZER;
    struct timespec next           = { 0, 0 };

    (void)p_arg;
    (void)clock_gettime(CLOCK_MONOTONIC, &next);

    for (;;)
    {
        /* 2. Receive: copy only when new; result goes to the cursor's event / n_lost */
        ubk_subscribe(UBK_TOPIC_WEAPON_STATE, &weapon_sub, &current_weapon, sizeof(current_weapon));
        ubk_subscribe(UBK_TOPIC_SENSOR_IMU0, &imu0_sub, &current_imu0, sizeof(current_imu0));

        /* --- Control logic --- */
        switch (imu0_sub.event)
        {
            case UBK_EVT_NEW_LOST:
                /* Published faster than the loop period; imu0_sub.n_lost samples skipped */
                break;
            case UBK_EVT_NEW:
                /* Update sensor fusion */
                break;
            case UBK_EVT_ERR_OS:
                /* Mutex failure (e.g. priority inheritance not supported) */
                break;
            default:
                /* No new data: keep using current_imu0 */
                break;
        }

        /* Always use the latest current_weapon, updated or not */
        if ((current_weapon.arm_state == WEAPON_STATE_ARM_STATE_ARMED) &&
            (current_weapon.is_locked == true))
        {
            /* Firing algorithm */
        }

        /* 3. Wait for the next period */
        next.tv_nsec += FUSION_PERIOD_NS;
        while (next.tv_nsec >= NS_PER_SEC)
        {
            next.tv_nsec -= NS_PER_SEC;
            next.tv_sec += 1;
        }
        (void)clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
    return NULL;
}
