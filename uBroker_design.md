# uBroker 요구사항 및 설계 명세 (v0.8)

> `prevWorks.md`(v0.1)를 바탕으로, 검토 결과와 결정사항을 반영해 다시 정리한 문서입니다.
> 참고 구현: uMsg(코드 구조), PX4 uORB(메시지 문법·동작 방식)
> 코딩 규칙: DAPA SCR-G (무기체계 소프트웨어 개발 및 관리 매뉴얼 [부록 6])

---

## 1. 개요

uBroker는 FreeRTOS 기반 임베디드 시스템을 위한 **정적 할당 Pub/Sub 메시지 브로커**입니다.

1. 개발자가 `.msg` 파일로 메시지를 정의합니다 (uORB 문법 + 확장).
2. `code_gen/ubroker_gen.py`가 메시지별 C 구조체와 **토픽 테이블**을 생성합니다. API는 토픽 ID를 인자로 받는 공통 함수입니다 (uORB의 `ORB_ID` 방식).
3. 토픽마다 정적 공유 메모리 1개를 두고, 발행자는 덮어쓰고 구독자는 최신값만 복사해 갑니다 (Mailbox).
4. 갱신 여부는 발행 순번으로 판별합니다. 브로커는 토픽별 순번만 갖고, 구독자는 자기 **구독 커서(`ubk_sub_t`)**에 마지막으로 받은 순번을 들고 있으므로 브로커는 구독자 목록이 없습니다. 메시지 구조체에는 사용자 필드만 들어갑니다.

---

## 2. 요구사항

### 2.1 기능 요구사항

| ID | 요구사항 |
|---|---|
| FR-01 | `.msg` 텍스트 파일로 메시지를 정의한다. 문법은 uORB를 따르고, FreeRTOS 타입·비트필드·기본값(ROS2 문법)을 확장으로 지원한다. |
| FR-02 | API는 토픽 ID(`ubk_topic_id_t`)를 인자로 받는 공통 함수로 제공한다. 메시지는 `const void *` + `size`로 넘기며, 토픽 메시지 크기와 다르면 거부한다. 토픽이 늘어도 늘어나는 코드는 테이블 항목뿐이다. |
| FR-03 | 구독자는 항상 가장 최근에 발행된 값 1개만 받는다 (Mailbox). |
| FR-04 | 수신은 Non-blocking이다. 결과는 커서의 `event`로 알려 주며, 새 데이터가 없으면 로컬 변수는 직전 값을 유지한다. |
| FR-05 | ISR 안에서 발행할 수 있는 전용 API를 제공한다. |
| FR-06 | 메시지의 모든 필드는 초기화된다. 기본값이 지정되면 그 값, 아니면 0이다. |
| FR-07 | 한 토픽의 발행자는 하나로 제한한다 (advertise로 받는 발행 핸들 `ubk_pub_t`). |
| FR-08 | 같은 구조체로 여러 토픽(다중 인스턴스)을 정의할 수 있다 (`# TOPICS`). |
| FR-09 | 직전 수신 이후 놓친 발행 횟수를 알 수 있다 (커서의 `n_lost`). |
| FR-10 | 전체 토픽 초기화 함수(`ubk_init_all`)를 생성한다. 초기화는 발행 전에만 가능하다. |
| FR-11 | 생성기는 입력 폴더와 출력 폴더를 인자로 받는다. |

### 2.2 비기능 요구사항

| ID | 요구사항 |
|---|---|
| NFR-01 | 동적 할당(`malloc`, `xQueueCreate` 등)을 사용하지 않는다. |
| NFR-02 | 구독자 수가 늘어도 브로커의 메모리 사용량과 발행 비용은 늘지 않는다 (O(1)). |
| NFR-03 | 발행 중 수신으로 데이터가 섞이는 현상(Tearing)이 없어야 한다. |
| NFR-04 | 구조체는 패딩 없이 정의한다 (`#pragma pack(push, 1)`). |
| NFR-05 | 생성 코드는 DAPA 무기체계 소프트웨어 코딩규칙(SCR-G) 제2장·제3장을 따른다. → 7장 참고 |

### 2.3 범위 밖 (의도적으로 제공하지 않음)

| 항목 | 사유 |
|---|---|
| Stale(오래된 데이터) 감지 | uORB와 같이 브로커가 다루지 않는다. 필요하면 사용자가 `timestamp` 필드로 판단한다. **기능이 없다는 점을 사용 시 반드시 고려할 것.** |
| 이벤트 기반 대기 (블로킹 수신) | 태스크 주기마다 `subscribe()` 후 커서의 `event`로 확인한다. |
| 메시지 버전/해시 | VCS로 관리한다. |
| OS 추상화 계층 | 현재 FreeRTOS 전용. 필요해지면 추가한다. |
| 멀티코어(SMP) | 추후 과제. 현재는 단일 코어 Critical Section 기준이다. |
| 가변 길이 필드 | uORB와 같이 고정 길이 배열만 지원한다. |

---

## 3. 설계 결정사항

### D1. Queue 대신 토픽당 정적 공유 메모리 1개
- Queue는 구독자마다 버퍼가 필요하지만(O(N)), 공유 메모리는 구독자 수와 관계없이 1개(O(1))다.

### D2. 동시성 제어: FreeRTOS Critical Section
- 태스크: `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()`
- ISR: `taskENTER_CRITICAL_FROM_ISR()` / `taskEXIT_CRITICAL_FROM_ISR()`
- 뮤텍스는 ISR에서 쓸 수 없고 우선순위 역전 위험이 있어 쓰지 않는다.
- **제약:** `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 우선순위가 높은 ISR에서는 uBroker API를 호출할 수 없다.

### D3. 갱신 판별: 구독 커서 `ubk_sub_t` + 이벤트 (v0.4)
- 브로커는 토픽마다 발행 순번 `seq`(uint32)를 토픽 상태 테이블에 둔다.
  - `0` = 초기화 후 한 번도 발행되지 않음
  - 발행할 때마다 1 증가하고, 순환 시 0은 건너뛴다 (`0xFFFFFFFF → 1`).
- 구독자는 토픽마다 커서 `ubk_sub_t`를 하나 갖고 `UBK_SUB_INITIALIZER`로 초기화한다.
  - `topic`: 첫 subscribe 때 그 토픽에 연결된다. 다른 토픽에 쓰면 `UBK_EVT_ERR_TOPIC`이고 아무것도 바꾸지 않는다.
  - `seq`: 마지막으로 받은 순번. 브로커 순번과 다르면 새 데이터다.
  - `n_lost`: 이번 호출에서 놓친 발행 횟수 (첫 수신은 0).
  - `event`: 이번 호출 결과.

| event | 의미 | `p_msg` |
|---|---|---|
| `UBK_EVT_NONE` | subscribe 호출 전 | - |
| `UBK_EVT_NO_DATA` | 토픽이 아직 발행되지 않음 | 그대로 |
| `UBK_EVT_NO_NEW` | 새 메시지 없음 | 그대로 (직전 값) |
| `UBK_EVT_NEW` | 새 메시지, 놓친 것 없음 | 복사됨 |
| `UBK_EVT_NEW_LOST` | 새 메시지, 중간에 `n_lost`개 놓침 | 복사됨 |
| `UBK_EVT_ERR_NULL` | `p_msg`가 NULL | - |
| `UBK_EVT_ERR_TOPIC` | 다른 토픽에 연결된 커서 | 그대로 |

- `p_sub`가 NULL이면 결과를 기록할 곳이 없으므로 아무 동작도 하지 않는다.

**경과**
- 목적: 새 데이터 판별을 구독자 쪽 상태로 처리해 브로커에 구독자 목록이 없게 한다 (구독자 무제한, O(1)).
- v0.1~v0.3: 순번을 메시지 구조체에 내장했다 (`_heartbeat` → `_seq` → `ubk_seq`, 맨 아래). 별도 구독 객체보다 사용이 간단하다는 이유였다.
- 드러난 문제: nested 메시지마다 무의미한 순번(4B)이 들어감, 같은 변수로 여러 토픽을 받거나 init/대입하면 오판, 발행 측에는 쓸모없는 필드, 외부 전송 시 내부 값이 섞임, 사용자가 수정 가능.
- v0.4 결정: 순번을 구조체에서 빼고 범용 커서로 옮겼다. subscribe는 반환값이 없고, 결과는 커서의 `event`/`n_lost`로 전달한다 (`_with_lost` 함수는 통합되어 삭제).

### D4. 발행자 제한: advertise 발행 핸들
- 이름 (v0.8): 구독 쪽 `ubk_sub_t`와 짝을 맞춰 `ubk_token_t`를 `ubk_pub_t`로 바꿨다 (`UBK_PUB_INVALID`, `UBK_ERR_INVALID_PUB`). 값과 동작은 같다.
- `ubk_advertise(id, &pub)`은 토픽당 **한 번만** 성공하고 발행 핸들(`ubk_pub_t`)을 준다. 두 번째 호출은 `UBK_ERR_ALREADY_ADVERTISED`를 반환한다.
- `ubk_publish(id, pub, ...)`는 발행 핸들이 그 토픽의 것이 아니면 `UBK_ERR_INVALID_PUB`을 반환한다.
- 디버그 옵션 `UBK_CFG_CHECK_PUBLISHER_TASK=1`:
  - advertise한 태스크를 기록하고, 다른 태스크가 publish하면 `UBK_ERR_WRONG_TASK`를 반환한다.
  - 스케줄러 시작 전(태스크 핸들 없음)에 advertise하면 검사하지 않는다 (ISR 발행자용).

### D5. 다중 인스턴스: `# TOPICS` 지시어 (uORB 방식)
- `# TOPICS sensor_imu0 sensor_imu1`이라고 쓰면 구조체 `sensor_imu_t` 하나에 토픽 ID `UBK_TOPIC_SENSOR_IMU0`, `UBK_TOPIC_SENSOR_IMU1`이 생성된다.
- 인스턴스는 각각 별도 토픽 ID이므로 인스턴스 번호 인자가 없다.
- `# TYPE_ONLY` (v0.7): 다른 메시지의 필드 타입으로만 쓰는 메시지(예: `vector3`)에 쓴다.
  - 생성: 구조체, 상수, `<MSG>_INITIALIZER` (`ubk_msgs.h`가 include하므로 `ubroker.h`만 include하면 사용 가능)
  - 생략: 토픽 ID, 기본값, 공유 메모리, 토픽 정보 테이블 행
  - `# TOPICS`와 같이 쓰면 생성 오류

### D8. 공통 API + 토픽 테이블 (v0.5)
- 토픽 ID는 생성된 `enum ubk_topic_id_t`이다. 공통 함수는 ID로 테이블을 찾아 처리한다.
  - `UBK_TOPIC_INVALID = 0`: 초기화 값과 같으므로 무효 ID로 예약한다 (v0.6). 실제 토픽은 1부터.
  - 끝은 `UBK_TOPIC_COUNT` (유효 범위 `1 ~ UBK_TOPIC_COUNT - 1`)
- 테이블 (배열 크기 `UBK_TOPIC_COUNT`, [0]은 무효 자리)
  - `g_ubk_topic_meta[]` (const, ROM, 생성 파일 `ubk_msgs.c`): 공유 메모리 주소, 기본값 주소, 메시지 크기, 토픽 키(발행 핸들 값)
  - `s_topic_state[]` (RAM, 고정 파일 `ubroker.c` 내부 static): 발행 순번, advertise 여부, (옵션) 발행 태스크
  - [0] 자리 낭비는 ROM 16바이트 + RAM 8바이트(32bit 기준)이며, 인덱스 보정 없이 ID로 바로 접근하기 위해 감수한다.
- 타입 안전: `void *`로 받으므로 컴파일러가 타입을 검사하지 않는다. 대신 호출자가 `sizeof(변수)`를 넘기고 브로커가 토픽 메시지 크기와 비교한다 (`UBK_ERR_SIZE` / `UBK_EVT_ERR_SIZE`). **크기가 같은 다른 타입은 잡지 못한다.**
- 잘못된 ID(`UBK_TOPIC_INVALID`, `UBK_TOPIC_COUNT` 이상)는 `UBK_ERR_INVALID_TOPIC` / `UBK_EVT_ERR_TOPIC`.
- 경과: v0.4까지는 토픽마다 전용 함수 6개(약 200줄)를 생성했다. 메시지가 많아지면 코드가 선형으로 늘어나므로 공통 함수로 바꿨다. 예제 기준 생성 `.c` 865줄 → 296줄, 토픽 1개 추가 시 4줄(데이터, 메타, 상태, enum).
- 파일 분리 (v0.6): 메시지 정의에 의존하는 부분만 생성하고, 공통 코드는 고정 파일로 재사용한다.

| 구분 | 파일 | 내용 |
|---|---|---|
| 고정 (`core/`) | `ubk_types.h` | 결과 코드, 발행 핸들, 구독 커서, 이벤트, 토픽 정보 타입 `ubk_topic_meta_t`, 설정 매크로 |
| 고정 (`core/`) | `ubroker.h` / `ubroker.c` | 공통 API 선언/구현, 토픽 상태 테이블 |
| 생성 | `<msg>.h` | 구조체, 상수, 초기화자 |
| 생성 | `ubk_msgs.h` / `ubk_msgs.c` | 메시지 헤더 묶음, 토픽 ID, 기본값, 공유 메모리, 토픽 정보 테이블 |

  - 의존 방향: `ubroker.c` → `ubroker.h` → `ubk_msgs.h`(생성) → `<msg>.h`(생성) → `ubk_types.h`. 생성 파일은 `ubroker.*`를 참조하지 않는다.
- 메시지별 초기화 함수(`ubk_init_<msg>_msg`)는 없앴다. `<MSG>_INITIALIZER`로 선언 시 초기화하고, 다시 초기화할 때는 `x = (<msg>_t)<MSG>_INITIALIZER;`를 쓴다.

### D6. 코드 생성기: Python
- 메시지 구조체와 토픽 테이블(`<msg>.h`, `ubk_msgs.h/.c`)만 생성한다. 공통 코드(`core/`)는 생성하지 않는다.
- 문법·타입·범위·비트필드를 생성 시점에 검사하고, 오류가 하나라도 있으면 아무것도 생성하지 않는다.
- 내용이 바뀐 파일만 다시 쓴다 (증분 생성, 6.3 참고).
- 생성 파일에는 한 줄 안내 주석(`Auto-generated ... DO NOT EDIT`)만 넣는다. 생성 시각·작성자는 넣지 않는다 (VCS diff 방지, 가독성).
- 주석은 짧게 유지한다. 함수 머리 주석은 넣지 않고, 필요한 곳에만 한 줄 주석을 단다.

### D7. 패딩 제거: `#pragma pack(push, 1)` / `#pragma pack(pop)`
- `push`/`pop`을 써서 다른 헤더의 정렬 설정에 영향을 주지 않는다.
- **주의:** packed 멤버의 주소(`&msg.field`)를 포인터로 넘기면 비정렬 접근이 되어 HardFault가 날 수 있다 (Cortex-M0 전부, M3 이상에서도 `LDRD`/FPU 로드). 멤버는 값으로 복사해서 사용한다.
- pack(1)을 써도 비트필드의 비트 배치 순서는 컴파일러가 정한다. 구조체를 그대로 외부(통신·파일)로 내보내지 않는다.

---

## 4. 메시지 정의 문법 (DSL)

### 4.1 문법 요약

```
# 맨 앞 주석 블록은 메시지 설명이 됩니다
TYPE  NAME = VALUE                 # 상수 (NAME 은 대문자)
TYPE  name                         # 필드 (기본값 0)
TYPE  name  DEFAULT                # 필드 + 기본값 (ROS2 문법)
TYPE[N]  name  [v1, v2, ...]       # 고정 길이 배열 (+ 기본값)
uint8 name : BITS  [DEFAULT]       # 비트필드 (확장 문법)
msgname  name                      # 중첩 메시지
# TOPICS topic_a topic_b           # 다중 토픽 (uORB)
# TYPE_ONLY                        # 자료형 전용, 토픽 없음 (확장)
```

### 4.2 타입

| `.msg` 타입 | C 타입 | 비고 |
|---|---|---|
| `bool` | `bool` | 기본값 `true` / `false` |
| `char` | `char` | 0 ~ 127 |
| `byte`, `uint8` / `int8` | `uint8_t` / `int8_t` | |
| `uint16` / `int16` | `uint16_t` / `int16_t` | |
| `uint32` / `int32` | `uint32_t` / `int32_t` | |
| `uint64` / `int64` | `uint64_t` / `int64_t` | |
| `float32` / `float64` | `float` / `double` | NaN/Inf 기본값 불가 |
| `TickType_t`, `BaseType_t`, `UBaseType_t`, `StackType_t`, `EventBits_t` | 동일 | FreeRTOS 타입. 크기가 포트마다 달라 비트필드 불가, 범위는 부호만 검사 |
| 다른 `.msg` 이름 | `<name>_t` | 중첩 메시지. 기본값은 중첩 메시지의 기본값 |

### 4.3 비트필드 (확장)
- `uint8`, `uint16`, `uint32`만 허용한다. 배열, bool, FreeRTOS 타입에는 쓸 수 없다.
- **채움 검사:** 연속된 같은 타입의 비트필드는 저장 단위(8/16/32 bit)를 **정확히** 채워야 한다. 남는 비트는 `reserved` 필드로 명시한다.

```
uint8 is_calibrated : 1
uint8 error_code    : 5
uint8 reserved      : 2      # 이 줄이 없으면 생성 오류 (6/8 bit)
```

### 4.4 기본값 (ROS2 문법)
- 이름 뒤에 공백으로 구분해 적는다. `=`를 쓰면 상수로 해석되므로 주의한다.
- 지원 형식: 정수(10진, `0x` 16진), 실수, `true`/`false`, 배열 `[a, b, c]`(개수가 길이와 같아야 함)
- 타입 범위와 비트 폭을 벗어나면 생성 오류가 난다.
- 중첩 메시지 필드에는 기본값을 지정할 수 없다.

### 4.5 이름 규칙
- 메시지 이름은 파일 이름에서 가져온다. `weapon_state.msg`와 `WeaponState.msg`(PX4 신규 방식) 모두 `weapon_state`가 된다.
- 필드 이름은 문자로 시작하고 31자 이하여야 한다 (SCR-G 제2장 2.다).
- `ubk_`로 시작하는 필드 이름(브로커 예약 접두어)과 C 예약어는 쓸 수 없다.
- 생성되는 static 변수 이름(`s_<topic>_data`, `s_<msg>_default`)도 31자 이하여야 하므로, 토픽 이름은 약 24자, 메시지 이름은 약 21자 이하로 제한된다 (생성기가 검사).
- 토픽 이름 `invalid`, `count`는 `UBK_TOPIC_INVALID`, `UBK_TOPIC_COUNT`와 충돌하므로 쓸 수 없다.
- 메시지 이름 `ubk_msgs`, `ubk_types`, `ubroker`와 `ubk_`로 시작하는 이름은 uBroker 파일과 충돌하므로 쓸 수 없다.
- 토픽 이름은 전체 메시지에서 유일해야 한다.

### 4.6 예시

```
# Weapon state payload
uint8 ARM_STATE_SAFE  = 0
uint8 ARM_STATE_ARMED = 1

uint64      timestamp_us
float32     target_x
float32     target_y
uint8       arm_state   : 2  0      # 기본값 SAFE
uint8       error_code  : 6
bool        is_locked        false
TickType_t  last_change_tick
```

---

## 5. API

토픽 ID는 `UBK_TOPIC_<TOPIC>` (TOPICS가 없으면 메시지 이름). `size`에는 `sizeof(메시지 변수)`를 넘긴다.

| API | 호출 위치 | 설명 |
|---|---|---|
| `ubk_result_t ubk_init_all(void)` | 부팅 시 1회 | 모든 토픽 초기화 |
| `ubk_result_t ubk_init_topic(ubk_topic_id_t id)` | 발행 전 | 토픽 공유 메모리 초기화. 이미 발행이 시작됐으면 `UBK_ERR_ALREADY_PUBLISHED` |
| `ubk_result_t ubk_advertise(ubk_topic_id_t id, ubk_pub_t *p_pub)` | 태스크 / 스케줄러 전 | 발행 권한 획득 (1회만 성공) |
| `ubk_result_t ubk_publish(ubk_topic_id_t id, ubk_pub_t pub, const void *p_msg, size_t size)` | 태스크 | 발행 |
| `ubk_result_t ubk_publish_from_isr(ubk_topic_id_t id, ubk_pub_t pub, const void *p_msg, size_t size)` | ISR | 발행 |
| `void ubk_subscribe(ubk_topic_id_t id, ubk_sub_t *p_sub, void *p_msg, size_t size)` | 태스크 | 새 데이터면 복사. 결과는 `p_sub->event`, `p_sub->n_lost` (D3 참고) |

publish 오류 검사 순서: 토픽 ID → NULL → 크기 → 발행 핸들 → (옵션) 발행 태스크.

생성 매크로:
- `<MSG>_<CONST>`: 상수
- `<MSG>_INITIALIZER`: 기본값 지정 초기화자
- `UBK_SUB_INITIALIZER`: 구독 커서 초기화자 (`ubk_types.h`)

### 5.1 사용 순서

```c
int main(void) {
    (void)ubk_init_all();                                   /* 1. 공유 메모리 초기화 */
    (void)ubk_advertise(UBK_TOPIC_SENSOR_IMU0, &s_tok);     /* 2. ISR 발행자는 여기서 권한 획득 */
    /* 3. 태스크 생성 → vTaskStartScheduler() */
}

/* 발행 */
weapon_state_t state = WEAPON_STATE_INITIALIZER;
(void)ubk_publish(UBK_TOPIC_WEAPON_STATE, pub, &state, sizeof(state));

/* 수신 */
ubk_sub_t      weapon_sub = UBK_SUB_INITIALIZER;
weapon_state_t weapon     = WEAPON_STATE_INITIALIZER;
ubk_subscribe(UBK_TOPIC_WEAPON_STATE, &weapon_sub, &weapon, sizeof(weapon));
if (weapon_sub.event == UBK_EVT_NEW) { /* ... */ }
```

전체 예제: `test/pub.c`, `test/sub.c`

---

## 6. 생성기 사용법

### 6.1 실행

```
python code_gen/ubroker_gen.py -i test/msg -o test/gen
```

- `--lang en|ko`: 오류 메시지와 `--help` 언어 (기본 `en`). 생성 코드의 주석은 언어와 관계없이 영문이다.

빌드에 넣을 것:
- include 경로: `core/`, 생성 폴더(`test/gen/`)
- 소스: `core/ubroker.c`, `<생성 폴더>/ubk_msgs.c`

### 6.2 출력 파일

| 파일 | 내용 |
|---|---|
| `<msg>.h` | 구조체, 상수, 초기화자 (함수 없음) |
| `ubk_msgs.h` | 메시지 헤더 묶음, 토픽 ID `ubk_topic_id_t`, 토픽 정보 테이블 선언 |
| `ubk_msgs.c` | 기본값, 공유 메모리, 토픽 정보 테이블 `g_ubk_topic_meta[]` |

`core/`의 `ubk_types.h`, `ubroker.h`, `ubroker.c`는 생성하지 않는다 (D8 참고).

### 6.3 증분 생성
- "증분 생성"은 **바뀐 것만 다시 만드는 것**을 말한다. 가변 길이 변수와는 관계없다.
- 생성기는 새로 만든 내용이 기존 파일과 같으면 파일을 다시 쓰지 않는다 (`unchanged` 출력).
- 그래서 빌드 시스템은 실제로 바뀐 소스만 다시 컴파일하고, VCS에도 불필요한 변경이 남지 않는다.
- 토픽을 추가·삭제하면 `ubk_msgs.h`(토픽 ID)가 바뀌므로 이를 include한 소스는 다시 컴파일된다.
- `.msg`를 삭제하면 해당 `<msg>.h`를 지운다 (`removed` 출력). 첫 줄이 생성기 안내 주석으로 시작하는 `.h/.c`만 지우고, 사용자 파일은 건드리지 않는다.
- 검증 오류가 있으면 쓰기·삭제를 모두 하지 않는다.

### 6.4 생성 시 검사 항목
문법 오류, 필드 없는 메시지, 알 수 없는 타입, 이름 중복, 예약 이름, 기본값 형식·범위, 배열 기본값 개수, 비트필드 타입·폭·채움, 중첩 메시지 순환 참조, 토픽 이름 중복, `TYPE_ONLY`/`TOPICS` 동시 사용

---

## 7. 코딩 규칙 (SCR-G) 적용 현황

근거: 무기체계 소프트웨어 개발 및 관리 매뉴얼 [부록 6] 무기체계 소프트웨어 코딩규칙 (DAPA SCR-G, `ref/` 폴더)

### 7.1 적용 기준에 대한 주의
SCR-G 제1장 4항은 **"관련 표준(MISRA-C:2012/2023 등) 준수를 원칙"**으로 하고, 표준 적용이 제한되는 사업에 한해 제3장 규칙(66개)을 따를 수 있도록 한다. 어떤 기준을 적용할지는 사업관리회의에서 확정한다.
uBroker 생성 코드는 **제2장(주석·명명)과 제3장(C/C++ 공통 46개, C 전용 5개)**을 기준으로 작성했다. 사업에서 MISRA-C를 적용 표준으로 정하면 7.3 항목을 다시 검토해야 한다.

### 7.2 생성 코드 적용 내역

| SCR-G 조항 | 적용 내용 |
|---|---|
| 2장 1.가 주석 형식 | `/* */`만 사용 |
| 2장 1.다 Nested Comment 금지 | `.msg` 주석의 `/*`, `*/`를 치환해서 삽입 |
| 2장 2.나 함수명 "동사 + 명사" | `ubk_init_topic`, `prv_write_topic`, `prv_check_publish` 등. 공통 API(`ubk_advertise`/`ubk_publish`/`ubk_subscribe`)는 대상(토픽)을 인자로 받는다 |
| 2장 2.다 변수명 | 생성 코드 내부의 static 변수 `s_`, 포인터 인자 `p_`, 횟수 변수 `n_` 접두어. `.msg` 필드 이름은 작성한 그대로 쓰며, 첫 글자 문자·31자 이하만 생성기가 검사 |
| 2장 2.라 매크로명 | 대문자와 `_`만 사용, `_`로 시작하는 이름 없음 |
| 3장 1.사 #if 매크로 사전 정의 | `UBK_CFG_CHECK_PUBLISHER_TASK`를 `#ifndef`로 기본값 정의 |
| 3장 1.자 Exit Point 1개 | 모든 함수의 `return` 1개 |
| 3장 1.카 한 줄에 한 명령문 | 적용 |
| 3장 1.타 else if 뒤 else | 모든 `else if`에 `else` |
| 3장 2.나 변수 초기화 | 모든 지역 변수를 선언 시 초기화 |
| 3장 4.가 float 동등 비교 금지 | 생성 코드에 float 비교 없음 |
| 3장 6.가 포인터 NULL 확인 | 내부 static 함수를 포함해 모든 포인터 역참조 전 NULL 확인 |
| 3장 나.5 구조체 초기화 | `<MSG>_INITIALIZER`로 모든 멤버를 명시적으로 초기화 |

### 7.3 의도적으로 적용하지 않은 항목
| SCR-G 조항 | 사유 |
|---|---|
| 2장 1.나 주석 필수 항목(작성자, 날짜, 수정이력 등) | 자동 생성 파일이며 이력은 VCS로 관리. 긴 주석은 가독성을 해침 |
| 2장 1.마 / 3장 1.가 주석 30% 이상 | 함수 머리 주석을 넣지 않으므로 생성 .c 파일은 30%에 못 미칠 수 있음 |

사업에서 위 항목 준수를 요구하면 생성기 템플릿에 다시 추가한다.

### 7.4 SCR-G 제3장에는 없으나 MISRA-C 적용 시 검토할 항목
- `uint8_t` 비트필드 (MISRA-C:2012 Rule 6.1, 구현 정의)
- `#pragma pack` (컴파일러 확장, Rule 1.2)
- 부호 없는 정수 순환(`seq + 1U`)은 의도된 동작이며 주석으로 명시함
- `void *` 메시지 인자와 `memcpy` (Rule 11.5 등): 크기 검사로 보완
- `uint32_t` → `ubk_topic_id_t` 변환 (`ubk_init_all` 반복문, Rule 10.5)

### 7.5 사용자 코드에 대한 안내
- 예제(`test/pub.c`, `test/sub.c`)는 변수 접두어와 매직 넘버 매크로화를 적용했다.
- `.msg` 필드 이름이 2장 2.다(첫 글자 문자, 31자 이하)를 어기면 생성기가 오류를 낸다.

## 8. 한계와 성능 특성

| 항목 | 내용 |
|---|---|
| 인터럽트 차단 시간 | publish/subscribe 1회당 메시지 크기에 비례한다 (구조체 복사). 큰 메시지(수백 바이트 이상)는 지연을 측정한 뒤 사용한다. uORB도 같은 구조다. |
| 구독자 수의 영향 | 브로커 메모리와 publish 비용은 O(1)이다. 다만 구독 호출마다 Critical Section에 들어가므로, 시스템 전체의 인터럽트 차단 시간은 구독 호출 횟수에 비례한다. 구독자의 로컬 사본 메모리도 구독자 수만큼 필요하다. |
| Stale 감지 | 없음. 발행자가 멈추면 구독자는 마지막 값을 계속 사용한다. |
| 커서 보호 | 사용자 코드가 커서 멤버를 수정하는 것은 막을 수 없다 (읽기 전용으로 사용). `p_sub`가 NULL이면 결과를 알릴 수 없다. |
| 놓친 횟수 | 2^32 - 1회 이상 놓치면 정확하지 않다 (실사용상 무시 가능). |
| 타입 검사 | 컴파일 시점 검사 없음. 크기 검사만 하므로 크기가 같은 다른 메시지를 넘기면 잡지 못한다. 토픽 ID와 변수 타입을 맞추는 것은 사용자 책임 (uORB와 같음). |
| 멀티코어 | 미지원. |

---

## 9. 남은 과제

1. 사업관리회의에서 코딩규칙 적용 기준(SCR-G 제3장 / MISRA-C) 확정. MISRA-C로 정하면 7.4 항목 처리
2. 정적 분석 도구로 생성 코드의 SCR-G 위반 여부 확인
3. 대상 MCU와 컴파일러에서 메시지 크기별 Critical Section 시간 측정
4. 대상 컴파일러에서 `#pragma pack` + 비트필드 배치 확인
5. 멀티코어(SMP) 대응 방식 결정
6. 실제 타깃(FreeRTOS)에서 통합 시험 (호스트 단위 시험은 `test/host/`에 있음, README '테스트' 참고)
