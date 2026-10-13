# 테스트

[English](testing.md) | 한국어

| 경로 | 내용 |
|---|---|
| `test/msg/` | 두 OS가 함께 쓰는 메시지 |
| `test/freertos/host/host_test.c` | FreeRTOS 호스트 테스트 (화이트박스, `core/ubroker.c`를 직접 include) |
| `test/freertos/host/host_stub/` | 호스트 테스트용 최소 FreeRTOS 스텁 (`FreeRTOS.h`, `task.h`) |
| `test/posix/posix_test.c` | POSIX 테스트: 기능 검사 + 다중 스레드 스트레스 (화이트박스) |

FreeRTOS 스텁 헤더는 FreeRTOS 포트가 그 이름으로 include하므로 실제 파일명을 그대로 씁니다.

## FreeRTOS 호스트 테스트

PC의 gcc로 실행합니다. `test/freertos/`에서:

```
python ../../code_gen/ubroker_gen.py -i ../msg -o gen
gcc -std=c99 -Wall -Wextra -pedantic -Wconversion -Wshadow -DUBK_CFG_CHECK_PUBLISHER_TASK=1 \
    -I ../../core -I gen -I host/host_stub host/host_test.c gen/ubk_msgs.c -o host/host_test.exe
./host/host_test.exe
```

## POSIX 테스트

POSIX 시스템(Linux, WSL, macOS 등)이 필요합니다. `test/posix/`에서:

```
python ../../code_gen/ubroker_gen.py -i ../msg -o gen --os posix
gcc -std=c99 -Wall -Wextra -pedantic -pthread -D_POSIX_C_SOURCE=200809L \
    -DUBK_CFG_PORT=UBK_PORT_POSIX -DUBK_CFG_CHECK_PUBLISHER_TASK=1 \
    -I ../../core -I gen posix_test.c gen/ubk_msgs.c ../../core/ubk_port_posix.c -o posix_test
./posix_test
```

- 우선순위 상속 mutex를 시험하려면 `-DUBK_CFG_POSIX_PRIO_INHERIT=1`을 추가합니다.
- ThreadSanitizer: `-fsanitize=thread -g`를 추가합니다 (Linux의 gcc/clang). 데이터 경쟁 보고가 없어야 합니다.

두 테스트 모두 `ALL PASSED (0 failures)`를 출력해야 하며, `-DUBK_CFG_CHECK_PUBLISHER_TASK=1` 없이 빌드하면 `#error`로 멈춥니다.

## 검사 항목

공통:
- 토픽 ID 0(`UBK_TOPIC_INVALID`)과 범위 밖 ID 거부
- 발행자 규칙: advertise 1회, 잘못된 발행 핸들, 다른 태스크/스레드의 발행
- 발행·구독의 크기 검사
- 이벤트 흐름: `NO_DATA` → `NEW` → `NO_NEW` → `NEW_LOST` (`n_lost` 포함)
- 순번 0도 유효한 순번이며, 순환(0xFFFFFFFF → 0) 시 놓친 횟수가 맞는지
- 오류 이벤트: `ERR_NULL`, `ERR_SIZE`, `ERR_TOPIC`, NULL 커서

FreeRTOS 호스트 테스트만: `QUEUE 4` 토픽(FIFO 순서, `n_pending`, 넘침이 `n_lost`로 집계됨, 늦게 시작한 구독자는 가장 오래된 것부터, 순번 순환 중에도 FIFO 순서 유지), `TYPE_ONLY` 메시지에 토픽이 없음, ISR 발행, `pack(1)` 구조체 크기, Critical Section 짝 맞춤

POSIX 테스트만 (스트레스): 발행 스레드 1개 × 구독 스레드 4개, 10만 회 발행, mailbox 토픽과 `QUEUE` 토픽 각각
- 메시지가 섞이지 않음: 검사하는 모든 필드가 같은 값
- 순번은 증가만 함
- 첫 수신 이후의 모든 발행은 받았거나 `n_lost`에 집계됨
- `QUEUE` 토픽: FIFO 순서로 도착하고, 빠진 것은 `n_lost`가 말한 만큼뿐

## 예제 컴파일 확인

```
cd test/freertos && gcc -std=c99 -Wall -Wextra -pedantic -I ../../core -I gen -I host/host_stub -c pub.c sub.c
cd test/posix    && gcc -std=c99 -Wall -Wextra -pedantic -pthread -D_POSIX_C_SOURCE=200809L \
                        -DUBK_CFG_PORT=UBK_PORT_POSIX -I ../../core -I gen -c pub.c sub.c
```
