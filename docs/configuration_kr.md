# 설정과 한계

[English](configuration.md) | 한국어

## 요구 환경

- FreeRTOS (단일 코어)
- C99 컴파일러
- Python 3.6 이상 (생성기에만 필요)

## 설정 매크로

`ubroker.h`를 include하기 전에 정의합니다. 보통 컴파일러 `-D` 옵션을 씁니다.

| 매크로 | 기본값 | 설명 |
|---|---|---|
| `UBK_CFG_CHECK_PUBLISHER_TASK` | `0` | `1`이면 advertise한 태스크가 아닌 곳에서 `ubk_publish`를 호출할 때 `UBK_ERR_WRONG_TASK`를 반환합니다. `INCLUDE_xTaskGetCurrentTaskHandle = 1`이 필요합니다. 스케줄러 시작 전에 advertise한 토픽(ISR 발행자)은 검사하지 않습니다 |

## FreeRTOS 요구사항

- 동시성은 뮤텍스가 아니라 FreeRTOS Critical Section(`taskENTER_CRITICAL` / `taskENTER_CRITICAL_FROM_ISR`)으로 처리합니다.
- uBroker를 호출하는 ISR의 우선순위는 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 이하여야 합니다.
- `ubk_init_all()`은 발행 전에 1회 호출합니다. 보통 `main()`에서 스케줄러 시작 전에 부릅니다.

## 주의사항

- **packed 구조체:** 메시지는 `#pragma pack(push, 1)`로 정의됩니다. 멤버 주소(`&msg.field`)를 포인터로 넘기지 마세요. 비정렬 접근으로 HardFault가 날 수 있습니다 (Cortex-M0 전부, M3 이상의 `LDRD`/FPU 로드). 멤버는 값으로 복사해서 쓰세요.
- **비트필드 배치**는 여전히 컴파일러가 정합니다. 구조체를 그대로 통신으로 보내거나 파일에 저장하지 마세요.
- **타입 검사:** API가 `void *`와 `size`를 받으므로 크기만 검사합니다. 크기가 같은 다른 메시지를 넘기면 잡지 못합니다. 토픽 ID와 변수 타입은 사용자가 맞춰야 합니다.
- **커서는 토픽마다 1개:** 하나의 `ubk_sub_t`를 다른 토픽에 쓰면 `UBK_EVT_ERR_TOPIC`입니다.

## 한계

| 항목 | 내용 |
|---|---|
| 인터럽트 차단 시간 | publish/subscribe마다 Critical Section 안에서 메시지 전체를 복사하므로 크기에 비례합니다. 큰 메시지(수백 바이트 이상)는 타깃에서 측정한 뒤 사용하세요. |
| 구독자가 많을 때 | 브로커 메모리와 발행 비용은 O(1)이지만, 구독 호출마다 Critical Section에 들어가고 구독자마다 메시지 사본을 가집니다. |
| Stale 데이터 | 감지하지 않습니다. 발행자가 멈추면 구독자는 마지막 값을 계속 씁니다. 필요하면 `timestamp` 필드로 판단하세요. |
| 놓친 횟수 | 2^32 - 1회 이상 놓치면 정확하지 않습니다 (실사용상 무시 가능). |
| 멀티코어(SMP) | 미지원 |
| 가변 길이 필드 | 미지원 (uORB와 같이 고정 길이 배열만) |
