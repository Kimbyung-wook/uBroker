# Testing

English | [한국어](testing_kr.md)

The host test checks broker behavior on a PC with gcc, using minimal FreeRTOS stubs.

| Path | Contents |
|---|---|
| `test/host/host_test.c` | White-box test (includes `core/ubroker.c` directly) |
| `test/host/host_stub/` | Minimal FreeRTOS stubs for host tests (`FreeRTOS.h`, `task.h`) |

The stub headers keep the real FreeRTOS file names because `core/ubroker.c` includes them by those names.

## Run

From the `test/` folder:

```
python ../code_gen/ubroker_gen.py -i msg -o gen
gcc -std=c99 -Wall -Wextra -pedantic -Wconversion -Wshadow -DUBK_CFG_CHECK_PUBLISHER_TASK=1 \
    -I ../core -I gen -I host/host_stub host/host_test.c gen/ubk_msgs.c -o host/host_test.exe
./host/host_test.exe
```

Expected output: `ALL PASSED (0 failures)`. The test must be built with `-DUBK_CFG_CHECK_PUBLISHER_TASK=1`, otherwise it stops with `#error`.

## What Is Tested

- Topic ID 0 (`UBK_TOPIC_INVALID`) and out-of-range IDs are rejected; `TYPE_ONLY` messages have no topic
- Publisher rules: advertise once, wrong publisher handle, wrong task
- Size check on publish and subscribe
- Event flow: `NO_DATA` → `NEW` → `NO_NEW` → `NEW_LOST` (with `n_lost`)
- Sequence wrap-around skips 0, and the lost count stays correct
- Independent subscribers, multi-instance topics
- Error events: `ERR_NULL`, `ERR_SIZE`, `ERR_TOPIC`, NULL cursor
- Struct sizes with `pack(1)`, balanced critical sections

## Example Compile Check

```
gcc -std=c99 -Wall -Wextra -pedantic -I ../core -I gen -I host/host_stub -c pub.c sub.c
```
