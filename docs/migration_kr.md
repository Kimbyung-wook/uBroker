# 기존 코드를 uBroker로 전환하기

[English](migration.md) | 한국어

태스크끼리 **전역변수와 FreeRTOS 큐**로 데이터를 주고받는 FreeRTOS 코드를 uBroker로 바꾸는 방법입니다. 전환은 사람이 하고, 반복 작업은 전환 도구(`tools/migrate/`)가 맡습니다. 도구는 Python 3 표준 라이브러리만 쓰므로 폐쇄망에 그대로 반입할 수 있습니다.

> 전환 도구는 별도로 배포하며, 이 저장소에는 포함되어 있지 않습니다.

## 절차

| 단계 | 사람 | 도구 |
|---|---|---|
| 1. 목록화 | 목록 검토, 누락 보완 | `ipc_inventory.py`가 IPC 목록(CSV) 생성 |
| 2. 분류 | 행마다 전환 방식 결정 (아래 표) | CSV에 추천 유형과 근거 표시 |
| 3. 메시지 정의 | `.msg` 확정, 토픽 이름 결정 | `struct2msg.py`가 C 구조체를 `.msg` 초안으로 변환 |
| 4. 생성 | - | `ubroker_gen.py --os freertos` (`.msg` 검증) |
| 5. 교체 | 한 토픽씩: 발행자 먼저, 다음 구독자, 매번 빌드 | - |
| 6. 잔여 검사 | 남은 참조 제거, `status`를 `done`으로 | `ipc_residual.py`가 남은 참조 보고 |
| 7. 검증 | 타깃 시험 (아래 체크리스트) | `UBK_CFG_CHECK_PUBLISHER_TASK=1` |

```
python tools/migrate/ipc_inventory.py <src> -o inventory.csv --exclude FreeRTOS-Kernel
python tools/migrate/struct2msg.py <src>/app_types.h sensor_data_t -o msg
python code_gen/ubroker_gen.py -i msg -o gen --os freertos
python tools/migrate/ipc_residual.py <src> inventory.csv
```

모든 도구는 `--lang ko`로 한국어 메시지를 출력합니다.

## 분류

| 기존 형태 | 전환 방식 | CSV의 `recommended` |
|---|---|---|
| 최신값만 필요한 전역변수 (상태, 센서값) | Mailbox 토픽 | `mailbox` |
| 모든 항목이 중요한 큐 (명령, 이벤트), 폴링 | `# QUEUE N` 토픽, N은 기존 길이 이상인 2의 거듭제곱 | `queue` |
| 태스크가 대기하는 큐 (`xQueueReceive(q, &x, timeout)`) | `# QUEUE N` 토픽 + 발행 후 `xTaskNotifyGive` | `queue+notify` |
| 여러 태스크가 쓰는 전역변수 | 쓰는 곳마다 토픽 1개, 또는 소유 태스크 1개가 발행 | `split-or-owner` |
| ISR이 쓰는 데이터 | 스케줄러 전에 advertise, `ubk_publish_from_isr` | `isr` 열 = `yes` |
| 길이 1 큐 / `xQueueOverwrite` | Mailbox 토픽 | `mailbox` |
| 작업 큐: 여러 소비자가 항목을 **나눠 가짐** | FreeRTOS 큐 유지 (uBroker는 모든 구독자에게 같은 메시지를 줌) | `keep` |
| 세마포어 용도의 큐 (항목 크기 0) | 유지 | `keep` |
| 한 태스크만 쓰는 변수 | IPC 아님, 그대로 둠 | `skip` |
| 스케줄러 전 `main()`에서만 씀 | 상수/설정: 유지하거나 1회 발행 토픽 | `review` |

**큰 버퍼**(수백 바이트 이상)도 FreeRTOS 큐를 유지하거나 포인터/인덱스를 전달합니다. uBroker는 critical section 안에서 메시지를 복사하기 때문입니다(성능 절 참고).

알림 방식에서는 발행자가 구독 태스크의 핸들을 알아야 합니다. 이 결합은 이 경우에만 허용합니다.

## 전환 전후 예시

### 전역변수 -> Mailbox 토픽

```c
/* 전 */
sensor_data_t g_sensor_data;                 /* sensor.c */
g_sensor_data = local;                       /* SensorTask */
if (g_sensor_data.is_valid != 0U) { }        /* LogTask */

/* 후 */
ubk_pub_t pub = UBK_PUB_INVALID;             /* SensorTask */
(void)ubk_advertise(UBK_TOPIC_SENSOR_DATA, &pub);
(void)ubk_publish(UBK_TOPIC_SENSOR_DATA, pub, &local, sizeof(local));

ubk_sub_t     sub    = UBK_SUB_INITIALIZER;  /* LogTask: 태스크·토픽마다 커서 1개 */
sensor_data_t sensor = SENSOR_DATA_INITIALIZER;
ubk_subscribe(UBK_TOPIC_SENSOR_DATA, &sub, &sensor, sizeof(sensor));
if (sensor.is_valid != 0U) { }               /* 새 값이 없으면 직전 값 유지 */
```

### 대기 큐 -> QUEUE 토픽 + 태스크 알림

```c
/* 전 */
(void)xQueueSend(g_cmd_queue, &cmd, 0U);                         /* CommTask */
if (xQueueReceive(g_cmd_queue, &cmd, portMAX_DELAY) == pdPASS)   /* ControlTask */
{
    handle(&cmd);
}

/* 후 (cmd_msg.msg 에 '# QUEUE 8') */
if (ubk_publish(UBK_TOPIC_CMD_MSG, pub, &cmd, sizeof(cmd)) == UBK_OK)   /* CommTask */
{
    (void)xTaskNotifyGive(g_control_task);
}

(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);                   /* ControlTask */
is_draining = true;
while (is_draining == true)
{
    ubk_subscribe(UBK_TOPIC_CMD_MSG, &cmd_sub, &cmd, sizeof(cmd));
    if ((cmd_sub.event == UBK_EVT_NEW) || (cmd_sub.event == UBK_EVT_NEW_LOST))
    {
        handle(&cmd);                        /* 오래된 것부터 */
    }
    else
    {
        is_draining = false;
    }
}
```

- `UBK_EVT_NO_NEW`가 나올 때까지 반복해서 받습니다. 알림 1번이 메시지 여러 개를 뜻할 수 있습니다.
- `UBK_EVT_NEW_LOST`는 큐가 넘쳤다는 뜻입니다(`cmd_sub.n_lost`개). 기존 큐는 이때 송신이 실패하거나 대기했습니다. 이 횟수를 기록하고, 넘치지 않도록 N을 정합니다.

### ISR 발행

```c
static ubk_pub_t s_rx_pub = UBK_PUB_INVALID;

void Uart_Init(void)                         /* vTaskStartScheduler() 전 */
{
    (void)ubk_advertise(UBK_TOPIC_UART_RX, &s_rx_pub);
}

void UART1_IRQHandler(void)
{
    (void)ubk_publish_from_isr(UBK_TOPIC_UART_RX, s_rx_pub, &rx, sizeof(rx));
}
```

### 쓰는 곳이 여럿일 때

```c
/* 전: ControlTask 와 CommTask 가 모두 g_mode 를 씀 */

/* 후, 방법 A: 쓰는 곳마다 토픽 1개, 읽는 쪽은 둘 다 구독 */
UBK_TOPIC_MODE_REQ_CONTROL, UBK_TOPIC_MODE_REQ_COMM      /* '# TOPICS mode_req_control mode_req_comm' */

/* 후, 방법 B: CommTask 는 요청을 발행, ControlTask 가 mode 를 소유하고 발행 */
```

### 주기당 1회 구독

전역변수를 직접 읽던 코드는 필드가 필요한 곳마다 전역변수를 읽는 경우가 많습니다. 이 읽기를 하나하나 `ubk_subscribe` 호출로 바꾸지 않습니다. 호출할 때마다 critical section에 들어가고 메시지 전체를 복사하기 때문입니다. 태스크 주기를 시작할 때 한 번 구독하고, 그 주기의 나머지에서는 로컬 사본을 씁니다.

```c
/* 피할 것: 필드 접근마다 구독 */
ubk_subscribe(UBK_TOPIC_SENSOR_DATA, &sub, &s, sizeof(s)); a = s.accel.x;
ubk_subscribe(UBK_TOPIC_SENSOR_DATA, &sub, &s, sizeof(s)); b = s.accel.y;

/* 권장: 주기마다 한 번 구독, 이후에는 로컬 사본 사용 */
ubk_subscribe(UBK_TOPIC_SENSOR_DATA, &sub, &s, sizeof(s));
a = s.accel.x;
b = s.accel.y;
```

- 호출 수가 "필드 접근 횟수"가 아니라 "주기당 토픽 수"가 됩니다.
- 한 주기에서 쓰는 모든 필드가 같은 메시지에서 나오므로 값끼리 일관됩니다.
- 보조 함수는 토픽을 다시 읽지 않고, 로컬 사본을 인자(`const sensor_data_t *p_s`)로 받습니다.

## 도구 설명

### ipc_inventory.py
- 다음을 찾습니다.
  - `xTaskCreate*`의 태스크 함수
  - ISR: 이름이 `IRQHandler` / `_ISR` / `Isr`로 끝나거나 `*FromISR`를 호출하는 함수
  - 파일 범위 변수
  - `xQueueCreate*`로 만든 큐와 모든 `xQueueSend*/Receive*/Peek/Overwrite` 호출
- 각 접근 위치는 간단한 호출 그래프로 태스크에 연결합니다. `?(함수)`는 어느 태스크에서도 도달하지 못한 함수입니다(예: 함수 포인터로 호출).
- CSV 열: `name, kind, type, defined_at, writers, readers, blocking, isr, recommended, queue_n, reason, status`. UTF-8(BOM)이라 Excel에서 바로 열립니다.
- **찾지 못하는 것**
  - 매크로, 포인터(`p = &g_x`), 함수 포인터를 통한 접근
  - 전역변수를 가리는 같은 이름의 지역변수
  - 다른 파일에 있는 같은 이름의 `static` 변수(하나로 합쳐짐)
- 커널과 벤더 폴더는 `--exclude`로 제외합니다.

### struct2msg.py
- 타입, 배열(숫자나 `#define` 길이), 비트필드, 중첩 구조체(`TYPE_ONLY` 초안)를 변환하고 필드 주석을 옮깁니다.
- 비트필드가 저장 단위를 다 채우지 않으면 `reserved` 필드를 추가합니다. 생성기가 요구하는 조건입니다.
- `int`, `long`, `short`는 일반적인 32비트 MCU 크기로 바꾸고 따로 알려 줍니다. enum은 값이 들어가는 가장 작은 부호 없는 타입으로 저장합니다.
- 포인터, union, 다차원 배열, 알 수 없는 타입은 `# TODO` 줄로 남깁니다. TODO가 남아 있으면 종료 코드가 2입니다.
- **uBroker 구조체는 packed**(`#pragma pack(1)`)입니다. 기존 메모리 배치에 의존하는 코드(바이트 단위 복사, DMA, 바이너리 로그)를 확인하세요.

### ipc_residual.py
- `status`가 `done`(또는 `--status <값>`)인 행을 검사합니다. 주석과 문자열은 무시합니다.
- 종료 코드는 0이면 남은 참조 없음, 1이면 남은 참조 있음입니다. 빌드 관문으로 쓸 수 있습니다.

### topic_graph.py
- 이미 uBroker를 쓰는 코드의 토픽 그래프(Pub/Sub Graph, Computation Graph라고도 함)를 그립니다.
  - 노드: 태스크, ISR, 토픽
  - 화살표: 발행(노드 -> 토픽), 구독(토픽 -> 노드)
- 인자가 `UBK_TOPIC_*`인 `ubk_advertise` / `ubk_publish*` / `ubk_subscribe` 호출을 찾고, `ipc_inventory.py`와 같은 호출 그래프로 태스크에 연결합니다.
- 출력
  - `<out>.mmd`: Mermaid
  - `<out>.dot`: Graphviz. `dot -Tsvg <out>.dot -o <out>.svg`로 그림 파일을 만듭니다.
  - `<out>_n2.csv`: N² 행렬(발행 × 구독). 규모가 클 때 씁니다.
- `--msg <폴더>`는 큐 길이를 표시하고, `--focus <태스크 또는 토픽>`은 그 주변만 그립니다.
- 다음도 함께 알려 줍니다.
  - 구독자나 발행자가 없는 토픽
  - 발행자가 여럿인 토픽
  - `.msg`에는 있지만 코드에서 쓰지 않는 토픽
  - 토픽 ID가 상수가 아닌 호출

## 검증 체크리스트

- [ ] 통합 기간에는 `UBK_CFG_CHECK_PUBLISHER_TASK=1`로 빌드합니다. 다른 태스크에서 발행하면 `UBK_ERR_WRONG_TASK`가 납니다.
- [ ] 모든 `ubk_advertise`(토픽당 1회)와 `ubk_publish`의 반환값을 확인합니다.
- [ ] 구독 태스크는 토픽마다 자기 `ubk_sub_t`를 `UBK_SUB_INITIALIZER`로 초기화해 둡니다.
- [ ] 발행 전에 `ubk_init_all()`을 1회 실행합니다.
- [ ] QUEUE 토픽은 `n_lost`를 기록합니다. 정상 동작에서는 0이어야 합니다.
- [ ] 큐에서 대기하던 코드는 알림을 기다리고 토픽을 끝까지 비웁니다.
- [ ] 각 태스크는 토픽을 주기당 한 번 구독하고, 로컬 사본으로 처리합니다.
- [ ] `ipc_residual.py`의 남은 참조가 0건입니다.
- [ ] 타이밍: 전환 전후의 태스크 실행 시간과 인터럽트 지연을 비교합니다(성능 절 참고).

## 성능

POSIX 포트로 측정했습니다(토픽 300개, 스레드 20~40개, i9-14900HX의 WSL2).

| 상황 | 결과 |
|---|---|
| 경합 없음 | 잠금 비용은 호출당 약 7~8 ns (256 B memcpy의 2~3배) |
| 20~40 스레드, 1 kHz | 경합 약 0.1%, API 시간 중 대기 1~3% |
| 한 토픽에 구독자 20개, 쉬지 않고 호출 | 처리량 약 10분의 1 |

- 잠금이 토픽마다 따로 있어서, 서로 다른 토픽끼리는 경합하지 않습니다.
- 경합은 여러 태스크가 **같은 토픽**을 매우 빠르게 호출할 때만 문제가 됩니다.
- FreeRTOS에서는 잠금이 critical section이라, 메시지 1개를 복사하는 동안 인터럽트가 막힙니다. 메시지는 작게(수십~수백 바이트) 유지하고, 큰 버퍼는 uBroker 밖에 둡니다.

### 500 MHz FreeRTOS 타깃 추정

아래는 **측정값이 아니라 추정값**입니다. 데이터 캐시가 켜진 Cortex-M7급 코어에서 메시지가 내부 SRAM/TCM에 있다고 가정했습니다. `ubk_publish`와 `ubk_subscribe` 모두 critical section 안에서 복사하므로 호출마다 아래 시간이 적용됩니다.

| 메시지 크기 | 호출당 인터럽트가 막히는 시간 (추정) |
|---|---|
| 64 B | 약 0.3~0.5 µs |
| 256 B | 약 0.7~1.5 µs |
| 1 KB | 약 2.5~5 µs |

- **고정 비용:** 검사, critical section 진입·종료, 순번 갱신에 약 100~200 사이클(0.2~0.4 µs)
- **복사:** 대기 사이클을 포함해 사이클당 약 0.5~1 바이트
- **외부 SDRAM이나 캐시를 끈 메모리**에서는 복사가 몇 배 느려질 수 있습니다. 토픽 데이터(`ubk_msgs.c`)는 내부 RAM에 배치합니다.
- **새로 생기는 비용이 아닙니다.** FreeRTOS의 `xQueueSend` / `xQueueReceive`도 critical section 안에서 항목을 복사합니다(`prvCopyDataToQueue`). 같은 항목 크기의 큐를 QUEUE 토픽으로 바꾸면 인터럽트가 막히는 시간은 거의 같습니다.
- **실제로 달라지는 곳은 기존 전역변수입니다.** 단순 읽기·쓰기에는 critical section이 없었는데, 이제 생깁니다. 대신 읽는 쪽이 반쯤 쓰인 데이터를 보는 일이 없어집니다.

### 인터럽트가 사라지는가

보통은 사라지지 않습니다. critical section 중에 들어온 인터럽트는 **대기(pending)** 상태로 남았다가, critical section이 끝나는 즉시 실행됩니다. 즉 늦어질 뿐 사라지지는 않습니다.

- **일부 인터럽트만 막힙니다.** FreeRTOS는 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 이하 우선순위의 인터럽트만 막습니다. 이보다 우선순위가 높은 인터럽트는 uBroker 때문에 늦어지지 않습니다. 대신 그 인터럽트에서는 FreeRTOS·uBroker API(`ubk_publish_from_isr` 포함)를 호출하면 안 됩니다.
- **사라지는 경우.** 막혀 있는 시간이 인터럽트 주기보다 길 때만 생깁니다.
  - **같은 인터럽트가 2번 이상 발생:** pending 비트가 1개라서 막혀 있는 동안의 여러 번이 1번으로 합쳐집니다. 예: 엔코더 엣지마다 인터럽트로 세는 경우
  - **주변장치 넘침:** ISR이 늦게 실행되는 사이에 주변장치 FIFO가 넘칩니다. 예: FIFO가 작은 고속 UART·SPI
- **500 MHz 기준 판단 기준:** 메시지가 256 B 이하이면 막히는 시간이 약 1.5 µs 이하입니다. 이때 주기가 약 3 µs 이상, 즉 약 300 kHz 미만인 인터럽트는 안전합니다. 1 Mbps UART(바이트당 10 µs)는 FIFO가 없어도 안전합니다.
- **이보다 빠른 인터럽트**는 다음 중 하나로 처리합니다.
  - 우선순위를 `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높게 둡니다.
  - 하드웨어 카운터나 DMA를 씁니다.
  - 경합하는 토픽의 메시지를 작게 유지합니다.

### 참고 사례

규모가 비슷한 오픈소스 비행 제어 소프트웨어 두 가지와 비교합니다.

**PX4 (uORB, pub/sub)**
- 규모(소스 코드 정적 집계)
  - 메시지 정의 232종. 다중 인스턴스 때문에 실행 시 토픽은 더 많습니다.
  - 발행 지점 약 810곳, 구독 지점 약 1,445곳
  - 모듈 99개, 독립 태스크 42개
  - 드라이버와 제어기 대부분은 소수의 work queue 스레드에서 WorkItem으로 실행됩니다.
- 타깃: Pixhawk 6 계열은 STM32H7(Cortex-M7, 480 MHz)을 씁니다.
- 잠금: NuttX에서 uORB는 critical section 안에서 메시지를 복사합니다. uBroker FreeRTOS 포트와 같은 방식입니다.
- 의미: 이 규모와 이 잠금 방식으로 500 MHz 타깃과 비슷한 MCU에서 비행 제어가 동작합니다.
- 차이: uORB는 발행할 때 기다리는 구독자를 깨웁니다. uBroker에는 이런 콜백이 없으므로 `queue+notify` 방식을 씁니다.

**ArduPilot (ArduCopter)**
- pub/sub이 아니라, 공유 객체의 **함수를 호출**해서 데이터를 얻습니다(예: `AP::ahrs()`).
- 메인 스케줄러는 협력형 단일 스레드라서 이 호출에 잠금이 필요 없습니다. 다른 스레드(드라이버 백엔드)와 공유하는 데이터는 세마포어 안에서 복사하며, 비용은 uBroker와 비슷합니다.
- 여기서는 **태스크 시간 예산**의 참고로만 씁니다.
- 메인 스케줄러는 태스크 29개(1~400 Hz)이고, 최대 허용 시간은 50~350 µs(대부분 50~130 µs)입니다. 초당 약 1,405회 실행되며, 모든 태스크가 허용 시간을 다 쓰면 CPU의 약 11.6%입니다. 고속 루프(자세 추정·제어)는 이 표에 없습니다.

**이 예산에 대한 uBroker 비용** (500 MHz 추정, 메시지 128 B 이하, 호출당 약 0.5 µs)

| 태스크 1회 실행당 호출 수 | 초당 1,405회 실행 시 합계 | 50 µs 태스크에서 차지하는 비율 |
|---|---|---|
| 5회 | 약 3.5 ms/s (CPU 0.35%) | 약 5% |
| 20회 | 약 14 ms/s (CPU 1.4%) | 약 20% |

- 전체 CPU에서 차지하는 비율은 작습니다.
- 제약이 되는 것은 가장 짧은 태스크입니다. 다음을 지킵니다.
  - 주기당 1회 구독합니다("주기당 1회 구독" 절 참고).
  - 메시지는 256 B 이하로 유지합니다.
  - 50 µs 태스크에서 1 KB 메시지는 피합니다. 호출 1회가 예산의 5~10%를 차지합니다.

### 타깃에서 측정하기

- **막히는 시간**
  - critical section 진입·종료 시점에 GPIO를 토글하고 오실로스코프로 펄스 폭을 잽니다.
  - 또는 `ubk_port_lock`과 `ubk_port_unlock` 사이의 DWT 사이클 수 최대값을 기록합니다.
- **전환 전후 비교:** 같은 방법으로 전환 전과 후의 인터럽트 지연과 태스크 실행 시간을 비교합니다.

도구에는 시험용 샘플 코드(`tools/migrate/sample/`)와, 그 샘플에서 나와야 하는 목록(`expected.csv`)이 함께 들어 있습니다.
