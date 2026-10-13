# API Reference

English | [한국어](api_kr.md)

Include only `ubroker.h`. It brings in the message types and topic IDs.

## Topic IDs

Generated in `ubk_msgs.h`:

```c
typedef enum
{
    UBK_TOPIC_INVALID = 0,          /* reserved: the init value is never a valid topic */
    UBK_TOPIC_SENSOR_IMU0,          /* sensor_imu_t */
    UBK_TOPIC_SENSOR_IMU1,          /* sensor_imu_t */
    UBK_TOPIC_WEAPON_STATE,         /* weapon_state_t */
    UBK_TOPIC_COUNT
} ubk_topic_id_t;
```

Valid IDs are `1 ~ UBK_TOPIC_COUNT - 1`.

## Functions

The message is passed as `const void *` / `void *` together with `size`. Always pass `sizeof(your variable)`: the broker rejects a size that differs from the topic message size.

| Function | Called from | Returns |
|---|---|---|
| `ubk_result_t ubk_init_all(void)` | boot, once | first error, or `UBK_OK` |
| `ubk_result_t ubk_init_topic(ubk_topic_id_t id)` | before publishing | see below |
| `ubk_result_t ubk_advertise(ubk_topic_id_t id, ubk_pub_t *p_pub)` | task, or before the scheduler | see below |
| `ubk_result_t ubk_publish(ubk_topic_id_t id, ubk_pub_t pub, const void *p_msg, size_t size)` | task | see below |
| `ubk_result_t ubk_publish_from_isr(ubk_topic_id_t id, ubk_pub_t pub, const void *p_msg, size_t size)` | ISR | see below |
| `void ubk_subscribe(ubk_topic_id_t id, ubk_sub_t *p_sub, void *p_msg, size_t size)` | task | result in `*p_sub` |

### ubk_init_all / ubk_init_topic
- Copies the message default into the topic's shared memory.
- `UBK_ERR_INVALID_TOPIC`: invalid ID.
- `UBK_ERR_ALREADY_PUBLISHED`: the topic has already been published; nothing changes.

### ubk_advertise
- Succeeds **once per topic** and returns the publisher handle in `*p_pub`.
- `UBK_ERR_NULL_POINTER`: `p_pub` is NULL.
- `UBK_ERR_INVALID_TOPIC`, `UBK_ERR_ALREADY_ADVERTISED`: `*p_pub` is set to `UBK_PUB_INVALID`.
- An ISR cannot call it. Advertise before the scheduler starts and keep the handle for the ISR.

### ubk_publish / ubk_publish_from_isr
Checked in this order. The first failure is returned and nothing is published.

1. `UBK_ERR_INVALID_TOPIC`: invalid ID
2. `UBK_ERR_NULL_POINTER`: `p_msg` is NULL
3. `UBK_ERR_SIZE`: `size` differs from the topic message size
4. `UBK_ERR_INVALID_PUB`: `pub` is not this topic's publisher handle
5. `UBK_ERR_WRONG_TASK` (task version, only with `UBK_CFG_CHECK_PUBLISHER_TASK=1`): called from a task other than the advertiser

On success the message is copied inside a critical section and the topic sequence number is incremented.

### ubk_subscribe
Uses a subscription cursor owned by the subscriber. Declare one cursor per topic.

```c
typedef struct
{
    ubk_pub_t   topic;      /* bound topic key (UBK_PUB_INVALID: unbound) */
    uint32_t    seq;        /* last received sequence (0: none received) */
    uint32_t    n_lost;     /* messages missed in this call */
    ubk_event_t event;      /* result of this call */
} ubk_sub_t;

ubk_sub_t sub = UBK_SUB_INITIALIZER;
```

- The cursor binds to a topic on its first successful call. Using it with another topic gives `UBK_EVT_ERR_TOPIC`.
- If `p_sub` is NULL the call does nothing (there is nowhere to report the result).
- `p_msg` is written only for `UBK_EVT_NEW` / `UBK_EVT_NEW_LOST`. Otherwise it keeps its previous value.

| `event` | Meaning | `p_msg` |
|---|---|---|
| `UBK_EVT_NONE` | subscribe not called yet | - |
| `UBK_EVT_NO_DATA` | topic not published yet | unchanged |
| `UBK_EVT_NO_NEW` | no new message | unchanged |
| `UBK_EVT_NEW` | new message, nothing missed | copied |
| `UBK_EVT_NEW_LOST` | new message, `n_lost` messages missed | copied |
| `UBK_EVT_ERR_NULL` | `p_msg` is NULL | - |
| `UBK_EVT_ERR_SIZE` | `size` differs from the topic message size | unchanged |
| `UBK_EVT_ERR_TOPIC` | invalid ID, or cursor bound to another topic | unchanged |

`n_lost` is 0 on the first receive and for every event other than `UBK_EVT_NEW_LOST`.

## Result Codes (`ubk_result_t`)

| Code | Meaning |
|---|---|
| `UBK_OK` | success |
| `UBK_ERR_NULL_POINTER` | NULL pointer argument |
| `UBK_ERR_INVALID_TOPIC` | invalid topic ID |
| `UBK_ERR_SIZE` | size differs from the topic message size |
| `UBK_ERR_ALREADY_ADVERTISED` | the topic already has a publisher |
| `UBK_ERR_INVALID_PUB` | wrong publisher handle |
| `UBK_ERR_WRONG_TASK` | publish from a task other than the advertiser |
| `UBK_ERR_ALREADY_PUBLISHED` | init after publishing started |

## Generated Macros

- `<MSG>_<CONST>`: constants from the `.msg` file
- `<MSG>_INITIALIZER`: designated initializer with all defaults. To reset a variable: `x = (<msg>_t)<MSG>_INITIALIZER;`
- `UBK_SUB_INITIALIZER`, `UBK_PUB_INVALID`: in `ubk_types.h`

## Examples

### Task publisher

```c
void WeaponControlTask(void *p_params)
{
    ubk_pub_t pub = UBK_PUB_INVALID;
    weapon_state_t state = WEAPON_STATE_INITIALIZER;

    if (ubk_advertise(UBK_TOPIC_WEAPON_STATE, &pub) != UBK_OK)
    {
        configASSERT(0);    /* another task is already the publisher */
    }
    for (;;)
    {
        state.arm_state = WEAPON_STATE_ARM_STATE_ARMED;
        (void)ubk_publish(UBK_TOPIC_WEAPON_STATE, pub, &state, sizeof(state));
        vTaskDelay(pdMS_TO_TICKS(20U));
    }
}
```

### ISR publisher

```c
static ubk_pub_t s_imu0_pub = UBK_PUB_INVALID;

void ImuDriver_Init(void)   /* before vTaskStartScheduler() */
{
    (void)ubk_advertise(UBK_TOPIC_SENSOR_IMU0, &s_imu0_pub);
}

void IMU0_IRQHandler(void)
{
    sensor_imu_t imu = SENSOR_IMU_INITIALIZER;
    imu.accel.z = 9.81F;
    (void)ubk_publish_from_isr(UBK_TOPIC_SENSOR_IMU0, s_imu0_pub, &imu, sizeof(imu));
}
```

### Subscriber

```c
ubk_sub_t    imu0_sub = UBK_SUB_INITIALIZER;
sensor_imu_t imu0     = SENSOR_IMU_INITIALIZER;

for (;;)
{
    ubk_subscribe(UBK_TOPIC_SENSOR_IMU0, &imu0_sub, &imu0, sizeof(imu0));
    switch (imu0_sub.event)
    {
        case UBK_EVT_NEW_LOST:  /* imu0_sub.n_lost samples were skipped */
        case UBK_EVT_NEW:       /* update with imu0 */
            break;
        default:                /* keep using the previous imu0 */
            break;
    }
    vTaskDelay(pdMS_TO_TICKS(10U));
}
```

Full examples: [`test/pub.c`](../test/pub.c), [`test/sub.c`](../test/sub.c)
