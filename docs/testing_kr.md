# 테스트

[English](testing.md) | 한국어

호스트 테스트는 PC의 gcc와 최소 FreeRTOS 스텁으로 브로커 동작을 검사합니다.

| 경로 | 내용 |
|---|---|
| `test/host/host_test.c` | 화이트박스 테스트 (`core/ubroker.c`를 직접 include) |
| `test/host/host_stub/` | 호스트 테스트용 최소 FreeRTOS 스텁 (`FreeRTOS.h`, `task.h`) |

스텁 헤더는 `core/ubroker.c`가 그 이름으로 include하므로 실제 FreeRTOS 파일명을 그대로 씁니다.

## 실행

`test/` 폴더에서 실행합니다.

```
python ../code_gen/ubroker_gen.py -i msg -o gen
gcc -std=c99 -Wall -Wextra -pedantic -Wconversion -Wshadow -DUBK_CFG_CHECK_PUBLISHER_TASK=1 \
    -I ../core -I gen -I host/host_stub host/host_test.c gen/ubk_msgs.c -o host/host_test.exe
./host/host_test.exe
```

기대 결과: `ALL PASSED (0 failures)`. `-DUBK_CFG_CHECK_PUBLISHER_TASK=1` 없이 빌드하면 `#error`로 멈춥니다.

## 검사 항목

- 토픽 ID 0(`UBK_TOPIC_INVALID`)과 범위 밖 ID 거부, `TYPE_ONLY` 메시지에 토픽이 없음
- 발행자 규칙: advertise 1회, 잘못된 발행 핸들, 다른 태스크의 발행
- 발행·구독의 크기 검사
- 이벤트 흐름: `NO_DATA` → `NEW` → `NO_NEW` → `NEW_LOST` (`n_lost` 포함)
- 순번 순환 시 0을 건너뛰고 놓친 횟수가 맞는지
- 구독자 간 독립 동작, 다중 인스턴스 토픽
- 오류 이벤트: `ERR_NULL`, `ERR_SIZE`, `ERR_TOPIC`, NULL 커서
- `pack(1)` 구조체 크기, Critical Section 짝 맞춤

## 예제 컴파일 확인

```
gcc -std=c99 -Wall -Wextra -pedantic -I ../core -I gen -I host/host_stub -c pub.c sub.c
```
