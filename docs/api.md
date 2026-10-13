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
| `ubk_result_t ubk_publish_from_isr(ubk_topic_id_t id, ubk_pub_t pub, const void *p_msg, size_t size)` | ISR (FreeRTOS only) | see below |
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
- `UBK_ERR_OS` (POSIX): the topic mutex could not be created or locked.

### ubk_publish / ubk_publish_from_isr
Checked in this order. The first failure is returned and nothing is published.

1. `UBK_ERR_INVALID_TOPIC`: invalid ID
2. `UBK_ERR_NULL_POINTER`: `p_msg` is NULL
3. `UBK_ERR_SIZE`: `size` differs from the topic message size
4. `UBK_ERR_INVALID_PUB`: `pub` is not this topic's publisher handle
5. `UBK_ERR_WRONG_TASK` (task version, only with `UBK_CFG_CHECK_PUBLISHER_TASK=1`): called from a task/thread other than the advertiser
6. `UBK_ERR_OS` (POSIX): the topic mutex could not be locked

On success the message is copied while the topic is locked (FreeRTOS: critical section, POSIX: topic mutex) and the topic sequence number is incremented. On a `# QUEUE N` topic it goes into the next slot; when the queue is full, the oldest message is overwritten (the publisher never waits).

`ubk_publish_from_isr` exists only when `UBK_CFG_PORT == UBK_PORT_FREERTOS`. On POSIX, see the signal pattern below.

### ubk_subscribe
Uses a subscription cursor owned by the subscriber. Declare one cursor per topic.

```c
typedef struct
{
    ubk_pub_t   topic;      /* bound topic key (UBK_PUB_INVALID: unbound) */
    uint32_t    seq;        /* sequence of the last received message (meaningful only if is_valid) */
    bool        is_valid;   /* false: nothing received yet */
    uint32_t    n_lost;     /* messages overwritten before this one was read (this call) */
    uint32_t    n_pending;  /* messages still waiting after this one (this call) */
    ubk_event_t event;      /* result of this call */
} ubk_sub_t;

ubk_sub_t sub = UBK_SUB_INITIALIZER;
```

- The cursor binds to a topic on its first successful call. Using it with another topic gives `UBK_EVT_ERR_TOPIC`.
- If `p_sub` is NULL the call does nothing (there is nowhere to report the result).
- `p_msg` is written only for `UBK_EVT_NEW` / `UBK_EVT_NEW_LOST`. Otherwise it keeps its previous value.
- **One message per call, oldest first (FIFO).** A new cursor starts from the oldest message still held. On a mailbox topic (no `# QUEUE`) only the latest message is held, so this is simply the latest value.
- Sequence numbers run 0, 1, 2, ... and wrap to 0. Whether a value exists is shown by `is_valid` (cursor), not by a special sequence value.

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
| `UBK_EVT_ERR_OS` | POSIX: topic mutex could not be locked | unchanged |

`n_lost` is 0 on the first receive and for every event other than `UBK_EVT_NEW_LOST`. `n_pending` tells how many messages are still waiting; call again until `UBK_EVT_NO_NEW` to drain a queue:

```c
bool is_draining = true;
while (is_draining == true)
{
    ubk_subscribe(UBK_TOPIC_WEAPON_CMD, &cmd_sub, &cmd, sizeof(cmd));
    if ((cmd_sub.event == UBK_EVT_NEW) || (cmd_sub.event == UBK_EVT_NEW_LOST))
    {
        /* handle cmd (oldest first) */
    }
    else
    {
        is_draining = false;    /* NO_NEW / NO_DATA / error */
    }
}
```

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
| `UBK_ERR_OS` | POSIX: mutex init/lock failed |

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

### ISR publisher (FreeRTOS)

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

### Signal -> thread publisher (POSIX)

A signal handler must not call uBroker. It wakes a thread, and the thread publishes.

```c
static sem_t s_imu_ready;
static ubk_pub_t s_imu0_pub = UBK_PUB_INVALID;

static void on_imu_signal(int sig)
{
    (void)sig;
    (void)sem_post(&s_imu_ready);   /* async-signal-safe */
}

static void *ImuThread(void *p_arg)
{
    sensor_imu_t imu = SENSOR_IMU_INITIALIZER;
    (void)p_arg;
    (void)ubk_advertise(UBK_TOPIC_SENSOR_IMU0, &s_imu0_pub);   /* this thread is the publisher */
    for (;;)
    {
        if (sem_wait(&s_imu_ready) == 0)
        {
            (void)ubk_publish(UBK_TOPIC_SENSOR_IMU0, s_imu0_pub, &imu, sizeof(imu));
        }
    }
    return NULL;
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

Full examples: FreeRTOS [`test/freertos/`](../test/freertos), POSIX [`test/posix/`](../test/posix)
