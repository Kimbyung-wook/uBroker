# Testing

English | [한국어](testing_kr.md)

| Path | Contents |
|---|---|
| `test/msg/` | Messages shared by both OSes |
| `test/freertos/host/host_test.c` | FreeRTOS host test (white-box, includes `core/ubroker.c`) |
| `test/freertos/host/host_stub/` | Minimal FreeRTOS stubs for host tests (`FreeRTOS.h`, `task.h`) |
| `test/posix/posix_test.c` | POSIX test: functional checks + multi-thread stress (white-box) |

The FreeRTOS stub headers keep the real file names because the FreeRTOS port includes them by those names.

## FreeRTOS host test

Runs on a PC with gcc. From `test/freertos/`:

```
python ../../code_gen/ubroker_gen.py -i ../msg -o gen
gcc -std=c99 -Wall -Wextra -pedantic -Wconversion -Wshadow -DUBK_CFG_CHECK_PUBLISHER_TASK=1 \
    -I ../../core -I gen -I host/host_stub host/host_test.c gen/ubk_msgs.c -o host/host_test.exe
./host/host_test.exe
```

## POSIX test

Needs a POSIX system (Linux, WSL, macOS, ...). From `test/posix/`:

```
python ../../code_gen/ubroker_gen.py -i ../msg -o gen --os posix
gcc -std=c99 -Wall -Wextra -pedantic -pthread -D_POSIX_C_SOURCE=200809L \
    -DUBK_CFG_PORT=UBK_PORT_POSIX -DUBK_CFG_CHECK_PUBLISHER_TASK=1 \
    -I ../../core -I gen posix_test.c gen/ubk_msgs.c ../../core/ubk_port_posix.c -o posix_test
./posix_test
```

- Add `-DUBK_CFG_POSIX_PRIO_INHERIT=1` to test priority-inheritance mutexes.
- With ThreadSanitizer: add `-fsanitize=thread -g` (gcc/clang on Linux). It should report no data races.

Both tests print `ALL PASSED (0 failures)` and must be built with `-DUBK_CFG_CHECK_PUBLISHER_TASK=1`, otherwise they stop with `#error`.

## What Is Tested

Both tests:
- Topic ID 0 (`UBK_TOPIC_INVALID`) and out-of-range IDs are rejected
- Publisher rules: advertise once, wrong publisher handle, wrong task/thread
- Size check on publish and subscribe
- Event flow: `NO_DATA` → `NEW` → `NO_NEW` → `NEW_LOST` (with `n_lost`)
- Sequence 0 is a valid sequence; wrap-around (0xFFFFFFFF → 0) keeps the lost count correct
- Error events: `ERR_NULL`, `ERR_SIZE`, `ERR_TOPIC`, NULL cursor

FreeRTOS host test only: `QUEUE 4` topic (FIFO order, `n_pending`, overflow counted in `n_lost`, late subscriber starts from the oldest, FIFO order across wrap-around), `TYPE_ONLY` has no topic, ISR publish, struct sizes with `pack(1)`, balanced critical sections.

POSIX test only (stress): 1 publisher thread × 4 subscriber threads, 100,000 publishes, on a mailbox topic and on the `QUEUE` topic.
- No torn messages: every checked field holds the same value
- Sequence numbers only increase
- Every publish after the first receive is either received or counted in `n_lost`
- `QUEUE` topic: messages arrive in FIFO order, gaps only where `n_lost` says so

## Example Compile Check

```
cd test/freertos && gcc -std=c99 -Wall -Wextra -pedantic -I ../../core -I gen -I host/host_stub -c pub.c sub.c
cd test/posix    && gcc -std=c99 -Wall -Wextra -pedantic -pthread -D_POSIX_C_SOURCE=200809L \
                        -DUBK_CFG_PORT=UBK_PORT_POSIX -I ../../core -I gen -c pub.c sub.c
```
