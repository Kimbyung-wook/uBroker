/* uBroker publish example (task publish, ISR publish) */

#include "ubroker.h"
#include "FreeRTOS.h"
#include "task.h"

/* Weapon control task settings */
#define WEAPON_TASK_STACK_SIZE  (256U)  /* stack size (words) */
#define WEAPON_TASK_PRIORITY    (3U)    /* task priority */
#define WEAPON_TASK_PERIOD_MS   (20U)   /* publish period, 50 Hz */

/* An ISR cannot call advertise, so keep the publisher handle obtained during init. */
static ubk_pub_t s_imu0_pub = UBK_PUB_INVALID;

void WeaponControlTask(void *p_params)
{
    ubk_pub_t pub = UBK_PUB_INVALID;
    weapon_state_t state = WEAPON_STATE_INITIALIZER;

    (void)p_params;

    /* 1. Obtain publish rights (only one publisher per topic succeeds) */
    if (ubk_advertise(UBK_TOPIC_WEAPON_STATE, &pub) != UBK_OK)
    {
        /* Another task is already the publisher: design error */
        configASSERT(0);
    }

    for (;;)
    {
        /* 2. Update fields */
        state.timestamp_us = 0ULL;      /* replace with real system time */
        state.target_x = 1.0F;
        state.target_y = 2.0F;
        state.arm_state = WEAPON_STATE_ARM_STATE_ARMED;
        state.is_locked = true;
        state.last_change_tick = (uint32_t)xTaskGetTickCount();

        /* 3. Publish (UBK_ERR_SIZE if size differs from the topic message size) */
        (void)ubk_publish(UBK_TOPIC_WEAPON_STATE, pub, &state, sizeof(state));

        vTaskDelay(pdMS_TO_TICKS(WEAPON_TASK_PERIOD_MS));
    }
}

void ImuDriver_Init(void)
{
    (void)ubk_advertise(UBK_TOPIC_SENSOR_IMU0, &s_imu0_pub);
}

void IMU0_IRQHandler(void)
{
    /* Init to defaults, then write measurements (replace with real register values) */
    sensor_imu_t imu = SENSOR_IMU_INITIALIZER;

    imu.accel.x = 0.0F;
    imu.accel.y = 0.0F;
    imu.accel.z = 9.81F;
    imu.is_calibrated = 1U;
    imu.accel_healthy = 1U;
    imu.gyro_healthy = 1U;

    /* Publish with the ISR API */
    (void)ubk_publish_from_isr(UBK_TOPIC_SENSOR_IMU0, s_imu0_pub, &imu, sizeof(imu));
}

int main(void)
{
    /* 1. Init shared memory of all topics (once, before publishing) */
    (void)ubk_init_all();

    /* 2. Obtain publish rights for the ISR publisher */
    ImuDriver_Init();

    /* 3. Create tasks and start the scheduler */
    (void)xTaskCreate(WeaponControlTask, "weapon", WEAPON_TASK_STACK_SIZE, NULL,
                      WEAPON_TASK_PRIORITY, NULL);
    vTaskStartScheduler();

    /* Never reached once the scheduler starts */
    for (;;)
    {
    }
}
