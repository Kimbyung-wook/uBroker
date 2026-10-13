# API 상세

[English](api.md) | 한국어

`ubroker.h` 하나만 include하면 됩니다. 메시지 타입과 토픽 ID가 함께 들어옵니다.

## 토픽 ID

`ubk_msgs.h`에 생성됩니다.

```c
typedef enum
{
    UBK_TOPIC_INVALID = 0,          /* 예약: 초기화 값은 유효한 토픽이 아님 */
    UBK_TOPIC_SENSOR_IMU0,          /* sensor_imu_t */
    UBK_TOPIC_SENSOR_IMU1,          /* sensor_imu_t */
    UBK_TOPIC_WEAPON_STATE,         /* weapon_state_t */
    UBK_TOPIC_COUNT
} ubk_topic_id_t;
```

유효한 ID는 `1 ~ UBK_TOPIC_COUNT - 1`입니다.

## 함수

메시지는 `const void *` / `void *`와 `size`로 넘깁니다. `size`에는 항상 `sizeof(변수)`를 넘기세요. 토픽 메시지 크기와 다르면 브로커가 거부합니다.

| 함수 | 호출 위치 | 반환 |
|---|---|---|
| `ubk_result_t ubk_init_all(void)` | 부팅 시 1회 | 첫 오류 또는 `UBK_OK` |
| `ubk_result_t ubk_init_topic(ubk_topic_id_t id)` | 발행 전 | 아래 참고 |
| `ubk_result_t ubk_advertise(ubk_topic_id_t id, ubk_pub_t *p_pub)` | 태스크, 또는 스케줄러 시작 전 | 아래 참고 |
| `ubk_result_t ubk_publish(ubk_topic_id_t id, ubk_pub_t pub, const void *p_msg, size_t size)` | 태스크 | 아래 참고 |
| `ubk_result_t ubk_publish_from_isr(ubk_topic_id_t id, ubk_pub_t pub, const void *p_msg, size_t size)` | ISR (FreeRTOS 전용) | 아래 참고 |
| `void ubk_subscribe(ubk_topic_id_t id, ubk_sub_t *p_sub, void *p_msg, size_t size)` | 태스크 | 결과는 `*p_sub` |

### ubk_init_all / ubk_init_topic
- 메시지 기본값을 토픽 공유 메모리에 복사합니다.
- `UBK_ERR_INVALID_TOPIC`: 잘못된 ID
- `UBK_ERR_ALREADY_PUBLISHED`: 이미 발행이 시작된 토픽이며, 아무것도 바꾸지 않습니다.

### ubk_advertise
- 토픽당 **한 번만** 성공하고, 발행 핸들을 `*p_pub`에 돌려줍니다.
- `UBK_ERR_NULL_POINTER`: `p_pub`이 NULL
- `UBK_ERR_INVALID_TOPIC`, `UBK_ERR_ALREADY_ADVERTISED`: `*p_pub`은 `UBK_PUB_INVALID`가 됩니다.
- ISR에서는 호출할 수 없습니다. 스케줄러 시작 전에 advertise하고, 받은 발행 핸들을 ISR에서 씁니다.
- `UBK_ERR_OS` (POSIX): 토픽 mutex를 만들거나 잠그지 못함

### ubk_publish / ubk_publish_from_isr
아래 순서로 검사하며, 처음 실패한 결과를 반환하고 발행하지 않습니다.

1. `UBK_ERR_INVALID_TOPIC`: 잘못된 ID
2. `UBK_ERR_NULL_POINTER`: `p_msg`가 NULL
3. `UBK_ERR_SIZE`: `size`가 토픽 메시지 크기와 다름
4. `UBK_ERR_INVALID_PUB`: `pub`이 이 토픽의 발행 핸들이 아님
5. `UBK_ERR_WRONG_TASK` (태스크용, `UBK_CFG_CHECK_PUBLISHER_TASK=1`일 때만): advertise한 태스크/스레드가 아닌 곳에서 호출
6. `UBK_ERR_OS` (POSIX): 토픽 mutex를 잠그지 못함

성공하면 토픽을 잠근 채(FreeRTOS: Critical Section, POSIX: 토픽 mutex) 메시지를 복사하고 토픽 발행 순번을 1 올립니다. `# QUEUE N` 토픽은 다음 칸에 저장하며, 큐가 가득 차면 가장 오래된 메시지를 덮어씁니다 (발행자는 기다리지 않음).

`ubk_publish_from_isr`는 `UBK_CFG_PORT == UBK_PORT_FREERTOS`일 때만 있습니다. POSIX는 아래 시그널 패턴을 참고하세요.

### ubk_subscribe
구독자가 가진 구독 커서를 씁니다. 토픽마다 커서를 하나씩 선언하세요.

```c
typedef struct
{
    ubk_pub_t   topic;      /* 연결된 토픽 키 (UBK_PUB_INVALID: 미연결) */
    uint32_t    seq;        /* 마지막으로 받은 메시지의 순번 (is_valid 일 때만 의미 있음) */
    bool        is_valid;   /* false: 아직 받은 것 없음 */
    uint32_t    n_lost;     /* 이번 메시지 전에 읽지 못하고 덮어써진 개수 (이번 호출) */
    uint32_t    n_pending;  /* 이번 메시지 뒤에 아직 남은 개수 (이번 호출) */
    ubk_event_t event;      /* 이번 호출 결과 */
} ubk_sub_t;

ubk_sub_t sub = UBK_SUB_INITIALIZER;
```

- 커서는 처음 정상 호출할 때 그 토픽에 연결됩니다. 다른 토픽에 쓰면 `UBK_EVT_ERR_TOPIC`입니다.
- `p_sub`가 NULL이면 아무 동작도 하지 않습니다 (결과를 기록할 곳이 없음).
- `p_msg`는 `UBK_EVT_NEW` / `UBK_EVT_NEW_LOST`일 때만 바뀌고, 그 밖에는 직전 값을 유지합니다.
- **한 번 호출에 메시지 하나, 오래된 것부터 (FIFO).** 새 커서는 남아 있는 가장 오래된 메시지부터 읽습니다. mailbox 토픽(`# QUEUE` 없음)은 최신 메시지 하나만 보관하므로 곧 최신 값입니다.
- 순번은 0, 1, 2, ... 로 증가하고 넘치면 0으로 돌아갑니다. 값이 있는지는 특별한 순번이 아니라 커서의 `is_valid`로 나타냅니다.

| `event` | 의미 | `p_msg` |
|---|---|---|
| `UBK_EVT_NONE` | subscribe 호출 전 | - |
| `UBK_EVT_NO_DATA` | 아직 발행되지 않은 토픽 | 그대로 |
| `UBK_EVT_NO_NEW` | 새 메시지 없음 | 그대로 |
| `UBK_EVT_NEW` | 새 메시지, 놓친 것 없음 | 복사됨 |
| `UBK_EVT_NEW_LOST` | 새 메시지, 중간에 `n_lost`개 놓침 | 복사됨 |
| `UBK_EVT_ERR_NULL` | `p_msg`가 NULL | - |
| `UBK_EVT_ERR_SIZE` | `size`가 토픽 메시지 크기와 다름 | 그대로 |
| `UBK_EVT_ERR_TOPIC` | 잘못된 ID, 또는 다른 토픽에 연결된 커서 | 그대로 |
| `UBK_EVT_ERR_OS` | POSIX: 토픽 mutex를 잠그지 못함 | 그대로 |

`n_lost`는 첫 수신과 `UBK_EVT_NEW_LOST`가 아닌 모든 경우에 0입니다. `n_pending`은 아직 남은 개수입니다. 큐를 비우려면 `UBK_EVT_NO_NEW`가 나올 때까지 반복 호출합니다.

```c
bool is_draining = true;
while (is_draining == true)
{
    ubk_subscribe(UBK_TOPIC_WEAPON_CMD, &cmd_sub, &cmd, sizeof(cmd));
    if ((cmd_sub.event == UBK_EVT_NEW) || (cmd_sub.event == UBK_EVT_NEW_LOST))
    {
        /* cmd 처리 (오래된 것부터) */
    }
    else
    {
        is_draining = false;    /* NO_NEW / NO_DATA / 오류 */
    }
}
```

## 결과 코드 (`ubk_result_t`)

| 코드 | 의미 |
|---|---|
| `UBK_OK` | 성공 |
| `UBK_ERR_NULL_POINTER` | NULL 포인터 인자 |
| `UBK_ERR_INVALID_TOPIC` | 잘못된 토픽 ID |
| `UBK_ERR_SIZE` | 크기가 토픽 메시지 크기와 다름 |
| `UBK_ERR_ALREADY_ADVERTISED` | 이미 발행자가 있는 토픽 |
| `UBK_ERR_INVALID_PUB` | 잘못된 발행 핸들 |
| `UBK_ERR_WRONG_TASK` | advertise한 태스크가 아닌 곳에서 발행 |
| `UBK_ERR_ALREADY_PUBLISHED` | 발행 시작 후 초기화 시도 |
| `UBK_ERR_OS` | POSIX: mutex 초기화/잠금 실패 |

## 생성 매크로

- `<MSG>_<CONST>`: `.msg`의 상수
- `<MSG>_INITIALIZER`: 모든 기본값을 담은 지정 초기화자. 다시 초기화할 때: `x = (<msg>_t)<MSG>_INITIALIZER;`
- `UBK_SUB_INITIALIZER`, `UBK_PUB_INVALID`: `ubk_types.h`

## 예제

### 태스크 발행

```c
void WeaponControlTask(void *p_params)
{
    ubk_pub_t pub = UBK_PUB_INVALID;
    weapon_state_t state = WEAPON_STATE_INITIALIZER;

    if (ubk_advertise(UBK_TOPIC_WEAPON_STATE, &pub) != UBK_OK)
    {
        configASSERT(0);    /* 다른 태스크가 이미 발행자 */
    }
    for (;;)
    {
        state.arm_state = WEAPON_STATE_ARM_STATE_ARMED;
        (void)ubk_publish(UBK_TOPIC_WEAPON_STATE, pub, &state, sizeof(state));
        vTaskDelay(pdMS_TO_TICKS(20U));
    }
}
```

### ISR 발행 (FreeRTOS)

```c
static ubk_pub_t s_imu0_pub = UBK_PUB_INVALID;

void ImuDriver_Init(void)   /* vTaskStartScheduler() 전에 호출 */
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

### 시그널 → 스레드 발행 (POSIX)

시그널 핸들러에서는 uBroker를 호출하면 안 됩니다. 스레드를 깨우기만 하고, 스레드가 발행합니다.

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
    (void)ubk_advertise(UBK_TOPIC_SENSOR_IMU0, &s_imu0_pub);   /* 이 스레드가 발행자 */
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

### 구독

```c
ubk_sub_t    imu0_sub = UBK_SUB_INITIALIZER;
sensor_imu_t imu0     = SENSOR_IMU_INITIALIZER;

for (;;)
{
    ubk_subscribe(UBK_TOPIC_SENSOR_IMU0, &imu0_sub, &imu0, sizeof(imu0));
    switch (imu0_sub.event)
    {
        case UBK_EVT_NEW_LOST:  /* imu0_sub.n_lost 개 샘플을 건너뜀 */
        case UBK_EVT_NEW:       /* imu0 로 갱신 */
            break;
        default:                /* 직전 imu0 를 계속 사용 */
            break;
    }
    vTaskDelay(pdMS_TO_TICKS(10U));
}
```

전체 예제: FreeRTOS [`test/freertos/`](../test/freertos), POSIX [`test/posix/`](../test/posix)
