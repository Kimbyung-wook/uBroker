# 메시지 정의 문법 (.msg)

[English](msg_syntax.md) | 한국어

PX4 uORB 문법을 따르고, 몇 가지를 확장했습니다.

## 요약

```
# 맨 앞 주석 블록은 메시지 설명이 됩니다
TYPE  NAME = VALUE                 # 상수 (NAME 은 대문자)
TYPE  name                         # 필드 (기본값 0)
TYPE  name  DEFAULT                # 필드 + 기본값 (ROS2 문법)
TYPE[N]  name  [v1, v2, ...]       # 고정 길이 배열 (+ 기본값)
uint8 name : BITS  [DEFAULT]       # 비트필드 (확장)
msgname  name                      # 중첩 메시지
# TOPICS topic_a topic_b           # 같은 구조체로 토픽 여러 개 (uORB)
# TYPE_ONLY                        # 자료형 전용, 토픽 없음 (확장)
# QUEUE N                          # 최근 N개 보관, FIFO (확장)
```

- 필드 뒤의 `#` 주석은 생성 헤더의 필드 주석이 됩니다.
- 생성 구조체 이름은 `<msg>_t`이며 `#pragma pack(push, 1)`로 패딩 없이 정의됩니다.

## 타입

| `.msg` 타입 | C 타입 | 비고 |
|---|---|---|
| `bool` | `bool` | 기본값 `true` / `false` |
| `char` | `char` | 0 ~ 127 |
| `byte`, `uint8` / `int8` | `uint8_t` / `int8_t` | |
| `uint16` / `int16` | `uint16_t` / `int16_t` | |
| `uint32` / `int32` | `uint32_t` / `int32_t` | |
| `uint64` / `int64` | `uint64_t` / `int64_t` | |
| `float32` / `float64` | `float` / `double` | NaN/Inf 기본값 불가 |
| `TickType_t`, `BaseType_t`, `UBaseType_t`, `StackType_t`, `EventBits_t` | 동일 | FreeRTOS 타입, `--os freertos`에서만 사용 가능. 크기가 포트마다 달라 비트필드 불가, 범위는 부호만 검사 |
| 다른 `.msg` 이름 | `<name>_t` | 중첩 메시지. 기본값은 중첩 메시지의 기본값 |

## 상수

```
uint8 ARM_STATE_SAFE  = 0
uint8 ARM_STATE_ARMED = 1
```

- `#define <MSG>_<NAME>`으로 생성됩니다. 예: `WEAPON_STATE_ARM_STATE_ARMED`
- 타입은 기본 타입이어야 하고, `NAME`은 대문자여야 합니다.

## 기본값 (ROS2 문법)

- 이름 뒤에 공백으로 구분해 적습니다. `=`를 쓰면 상수로 해석됩니다.
- 형식: 정수(10진, `0x` 16진), 실수, `true`/`false`, 배열 `[a, b, c]`(개수가 길이와 같아야 함)
- 타입 범위나 비트 폭을 벗어나면 오류입니다.
- 중첩 메시지 필드에는 기본값을 지정할 수 없습니다.
- 기본값이 없는 필드는 모두 0으로 초기화됩니다 (`<MSG>_INITIALIZER`).

## 비트필드 (확장)

- `uint8`, `uint16`, `uint32`만 허용합니다. 배열, `bool`, FreeRTOS 타입에는 쓸 수 없습니다.
- **채움 검사:** 연속된 같은 타입의 비트필드는 저장 단위(8/16/32 bit)를 **정확히** 채워야 합니다. 남는 비트는 `reserved` 필드로 명시합니다.

```
uint8 is_calibrated : 1
uint8 error_code    : 5
uint8 reserved      : 2      # 이 줄이 없으면 오류 (6/8 bit)
```

## 토픽

- 지시자가 없으면 메시지 이름과 같은 토픽 1개가 생깁니다 (`UBK_TOPIC_<MSG>`).
- `# TOPICS a b`: 구조체 하나에 토픽 `UBK_TOPIC_A`, `UBK_TOPIC_B`가 생깁니다 (다중 인스턴스).
- `# QUEUE N`: 토픽이 최신 값 하나가 아니라 최근 N개 메시지를 보관합니다. 구독자는 각자 오래된 것부터 읽습니다 (FIFO). N은 1 ~ 65536 사이의 2의 거듭제곱이어야 합니다 (기본 1 = mailbox). 메모리는 토픽마다 N × 메시지 크기입니다. 메시지의 모든 토픽에 적용됩니다. 명령처럼 덮어쓰면 안 되는 사건에 씁니다.
- `# TYPE_ONLY`: 다른 메시지의 필드 타입으로만 쓰는 메시지(예: `vector3`)에 씁니다. 구조체, 상수, 초기화자만 생성하고 토픽 ID, 공유 메모리, 테이블 항목은 만들지 않습니다. `# TOPICS`나 `# QUEUE`와 함께 쓸 수 없습니다.

## 이름 규칙

- 메시지 이름은 파일 이름에서 가져옵니다. `weapon_state.msg`와 `WeaponState.msg` 모두 `weapon_state`가 됩니다.
- 메시지 이름과 토픽 이름은 소문자 `snake_case`여야 하고, 토픽 이름은 전체 메시지에서 유일해야 합니다.
- 필드 이름은 문자로 시작하고 31자 이하여야 하며, C 예약어나 `ubk_`로 시작하는 이름은 쓸 수 없습니다.
- 예약된 메시지 이름: `ubk_msgs`, `ubk_types`, `ubroker`, 그리고 `ubk_`로 시작하는 이름
- 예약된 토픽 이름: `invalid`, `count` (`UBK_TOPIC_INVALID`, `UBK_TOPIC_COUNT`와 충돌)
- 생성 변수 이름(`s_<topic>_data`, `s_<msg>_default`)도 31자 이하여야 하므로 토픽 이름은 약 24자, 메시지 이름은 약 21자 이하로 제한됩니다.

## 예시

```
# Weapon state payload
uint8 ARM_STATE_SAFE  = 0       # safe
uint8 ARM_STATE_ARMED = 1       # armed

uint64      timestamp_us            # measurement time (microseconds)
float32     target_x
float32     target_y
uint8       arm_state   : 2  0      # ARM_STATE_* (default: SAFE)
uint8       error_code  : 6
bool        is_locked        false
uint32      last_change_tick        # OS tick of the last state change
```

더 많은 예: [`test/msg/`](../test/msg)
