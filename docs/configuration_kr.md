# 설정과 한계

[English](configuration.md) | 한국어

## 요구 환경

| | FreeRTOS | POSIX |
|---|---|---|
| OS | FreeRTOS (단일 코어) | POSIX 스레드 (Linux, QNX 등) |
| 컴파일러 | C99 | C99, `-pthread -D_POSIX_C_SOURCE=200809L` |
| 소스 | `core/ubroker.c`, `<gen>/ubk_msgs.c` | 왼쪽 + `core/ubk_port_posix.c` |
| 생성기 | `--os freertos` (기본) | `--os posix` |

Python 3.6 이상은 생성기에만 필요합니다.

## 설정 매크로

`ubroker.h`를 include하기 전에 정의합니다. 보통 컴파일러 `-D` 옵션을 씁니다.

| 매크로 | 기본값 | 설명 |
|---|---|---|
| `UBK_CFG_PORT` | `UBK_PORT_FREERTOS` | 대상 OS: `UBK_PORT_FREERTOS` 또는 `UBK_PORT_POSIX`. 생성기 `--os`와 같아야 하며, 다르면 빌드가 `#error`로 멈춥니다 |
| `UBK_CFG_CHECK_PUBLISHER_TASK` | `0` | `1`이면 advertise한 태스크/스레드가 아닌 곳에서 `ubk_publish`를 호출할 때 `UBK_ERR_WRONG_TASK`를 반환합니다. FreeRTOS는 `INCLUDE_xTaskGetCurrentTaskHandle = 1`이 필요하고, 스케줄러 시작 전에 advertise한 토픽(ISR 발행자)은 검사하지 않습니다 |
| `UBK_CFG_POSIX_PRIO_INHERIT` | `0` | POSIX 전용. `1`이면 토픽 mutex에 우선순위 상속(`PTHREAD_PRIO_INHERIT`)을 적용합니다. 플랫폼이 지원하지 않으면 모든 API가 `UBK_ERR_OS` / `UBK_EVT_ERR_OS`를 반환합니다 |

## OS 포트

| | FreeRTOS 포트 | POSIX 포트 |
|---|---|---|
| 잠금 | Critical Section (`taskENTER_CRITICAL`) | 토픽마다 `pthread_mutex_t` 1개 |
| 잠금 실패 | 없음 | `UBK_ERR_OS` / `UBK_EVT_ERR_OS` |
| ISR / 시그널에서 발행 | `ubk_publish_from_isr` | 없음 (아래 참고) |
| 멀티코어 | 미지원 | 지원 |
| 발행자 식별 | `TaskHandle_t` | `pthread_t` (`pthread_equal`) |

포트 파일: `core/ubk_port.h`(인터페이스), `core/ubk_port_freertos.h`, `core/ubk_port_posix.h` + `core/ubk_port_posix.c`

### FreeRTOS
- uBroker를 호출하는 ISR의 우선순위는 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 이하여야 합니다.
- `ubk_init_all()`은 발행 전에 1회 호출합니다. 보통 `main()`에서 스케줄러 시작 전에 부릅니다.

### POSIX
- mutex는 처음 잠글 때(`pthread_once`) 만들어지며, 동적 할당은 없습니다.
- `ubk_publish_from_isr`는 선언되지 않습니다. 호출하면 선언되지 않은 함수가 되어, GCC 14 이상과 Clang에서는 컴파일 오류, 이전 컴파일러에서는 `-Werror=implicit-function-declaration`을 주면 컴파일 오류가 나고, 링크는 항상 실패합니다.
- 시그널 핸들러에서 uBroker를 호출하면 안 됩니다 (mutex는 async-signal-safe가 아님). 핸들러에서는 세마포어만 올리고 스레드에서 발행하세요.

```c
static void on_signal(int sig) { (void)sig; (void)sem_post(&s_ready); }      /* 핸들러: 깨우기만 */
static void *imu_thread(void *p) { for (;;) { (void)sem_wait(&s_ready); (void)ubk_publish(...); } }
```

## 주의사항

- **packed 구조체:** 메시지는 `#pragma pack(push, 1)`로 정의됩니다. 멤버 주소(`&msg.field`)를 포인터로 넘기지 마세요. 비정렬 접근으로 HardFault가 날 수 있습니다 (Cortex-M0 전부, M3 이상의 `LDRD`/FPU 로드). 멤버는 값으로 복사해서 쓰세요.
- **비트필드 배치**는 여전히 컴파일러가 정합니다. 구조체를 그대로 통신으로 보내거나 파일에 저장하지 마세요.
- **타입 검사:** API가 `void *`와 `size`를 받으므로 크기만 검사합니다. 크기가 같은 다른 메시지를 넘기면 잡지 못합니다.
- **커서는 토픽마다 1개:** 하나의 `ubk_sub_t`를 다른 토픽에 쓰면 `UBK_EVT_ERR_TOPIC`입니다.

## 한계

| 항목 | 내용 |
|---|---|
| 차단 시간 | publish/subscribe마다 토픽을 잠근 채 메시지 전체를 복사합니다 (FreeRTOS는 인터럽트 차단). 큰 메시지(수백 바이트 이상)는 타깃에서 측정한 뒤 사용하세요. |
| 구독자가 많을 때 | 브로커 메모리와 발행 비용은 O(1)이지만, 구독 호출마다 잠금을 잡고 구독자마다 메시지 사본을 가집니다. |
| 우선순위 역전 (POSIX) | `UBK_CFG_POSIX_PRIO_INHERIT=1`이 아니면, 토픽 mutex를 잡은 낮은 우선순위 스레드가 높은 우선순위 스레드를 지연시킬 수 있습니다. |
| Stale 데이터 | 감지하지 않습니다. 발행자가 멈추면 구독자는 마지막 값을 계속 씁니다. 필요하면 `timestamp` 필드로 판단하세요. |
| 놓친 횟수 | 2^32 - 1회 이상 놓치면 정확하지 않습니다 (실사용상 무시 가능). |
| 멀티코어 | FreeRTOS: 미지원, POSIX: 지원 |
| 가변 길이 필드 | 미지원 (uORB와 같이 고정 길이 배열만) |
