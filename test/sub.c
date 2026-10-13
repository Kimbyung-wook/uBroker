/* uBroker subscribe example */

#include "ubroker.h"
#include "FreeRTOS.h"
#include "task.h"

/* Sensor fusion task period */
#define FUSION_TASK_PERIOD_MS   (10U)   /* 100Hz */

void SensorFusionTask(void *p_params)
{
    /* 1. Declare message variables and one cursor per topic (sharing a cursor gives UBK_EVT_ERR_TOPIC) */
    weapon_state_t current_weapon = WEAPON_STATE_INITIALIZER;
    sensor_imu_t   current_imu0   = SENSOR_IMU_INITIALIZER;
    sensor_imu_t   current_imu1   = SENSOR_IMU_INITIALIZER;
    ubk_sub_t      weapon_sub     = UBK_SUB_INITIALIZER;
    ubk_sub_t      imu0_sub       = UBK_SUB_INITIALIZER;
    ubk_sub_t      imu1_sub       = UBK_SUB_INITIALIZER;
    bool           is_imu_new     = false;

    (void)p_params;

    for (;;)
    {
        /* 2. Receive: copy only when new; result goes to the cursor's event / n_lost */
        ubk_subscribe(UBK_TOPIC_WEAPON_STATE, &weapon_sub, &current_weapon, sizeof(current_weapon));
        ubk_subscribe(UBK_TOPIC_SENSOR_IMU0, &imu0_sub, &current_imu0, sizeof(current_imu0));
        ubk_subscribe(UBK_TOPIC_SENSOR_IMU1, &imu1_sub, &current_imu1, sizeof(current_imu1));

        /* --- Control logic --- */
        if (weapon_sub.event == UBK_EVT_NEW)
        {
            /* One-shot trigger on state change (e.g. sound an alarm) */
        }

        switch (imu0_sub.event)
        {
            case UBK_EVT_NEW_LOST:
                /* Published faster than the loop period; imu0_sub.n_lost samples skipped */
                is_imu_new = true;
                break;
            case UBK_EVT_NEW:
                is_imu_new = true;
                break;
            case UBK_EVT_NO_DATA:
                /* IMU0 has not been published yet */
                is_imu_new = false;
                break;
            default:
                is_imu_new = false;
                break;
        }

        if ((imu1_sub.event == UBK_EVT_NEW) || (imu1_sub.event == UBK_EVT_NEW_LOST))
        {
            is_imu_new = true;
        }

        if (is_imu_new == true)
        {
            /* Update sensor fusion */
        }

        /* Always use the latest current_weapon, updated or not */
        if ((current_weapon.arm_state == WEAPON_STATE_ARM_STATE_ARMED) &&
            (current_weapon.is_locked == true))
        {
            /* Firing algorithm */
        }

        vTaskDelay(pdMS_TO_TICKS(FUSION_TASK_PERIOD_MS));
    }
}
