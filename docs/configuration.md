# Configuration and Limits

English | [한국어](configuration_kr.md)

## Requirements

| | FreeRTOS | POSIX |
|---|---|---|
| OS | FreeRTOS (single core) | POSIX threads (Linux, QNX, ...) |
| Compiler | C99 | C99, `-pthread -D_POSIX_C_SOURCE=200809L` |
| Sources | `core/ubroker.c`, `<gen>/ubk_msgs.c` | same + `core/ubk_port_posix.c` |
| Generator | `--os freertos` (default) | `--os posix` |

Python 3.6 or later is needed for the generator only.

## Configuration Macros

Define these before including `ubroker.h`, typically with a compiler `-D` option.

| Macro | Default | Description |
|---|---|---|
| `UBK_CFG_PORT` | `UBK_PORT_FREERTOS` | Target OS: `UBK_PORT_FREERTOS` or `UBK_PORT_POSIX`. Must match the generator `--os`, otherwise the build stops with `#error` |
| `UBK_CFG_CHECK_PUBLISHER_TASK` | `0` | `1`: `ubk_publish` from any task/thread other than the one that advertised returns `UBK_ERR_WRONG_TASK`. FreeRTOS needs `INCLUDE_xTaskGetCurrentTaskHandle = 1`; a topic advertised before the scheduler starts (ISR publisher) is not checked |
| `UBK_CFG_POSIX_PRIO_INHERIT` | `0` | POSIX only. `1`: topic mutexes use priority inheritance (`PTHREAD_PRIO_INHERIT`). If the platform does not support it, every API returns `UBK_ERR_OS` / `UBK_EVT_ERR_OS` |

## OS Ports

| | FreeRTOS port | POSIX port |
|---|---|---|
| Lock | critical section (`taskENTER_CRITICAL`) | one `pthread_mutex_t` per topic |
| Lock failure | never | `UBK_ERR_OS` / `UBK_EVT_ERR_OS` |
| ISR / signal publish | `ubk_publish_from_isr` | not available (see below) |
| Multi-core | not supported | supported |
| Publisher identity | `TaskHandle_t` | `pthread_t` (`pthread_equal`) |

Port files: `core/ubk_port.h` (interface), `core/ubk_port_freertos.h`, `core/ubk_port_posix.h` + `core/ubk_port_posix.c`.

### FreeRTOS
- An ISR that calls uBroker must have a priority at or below `configMAX_SYSCALL_INTERRUPT_PRIORITY`.
- Call `ubk_init_all()` once before any publish, usually in `main()` before the scheduler starts.

### POSIX
- Mutexes are created on the first lock (`pthread_once`), with no dynamic allocation.
- `ubk_publish_from_isr` is not declared. Calling it is an undeclared function: a compile error on GCC 14+ / Clang, or with `-Werror=implicit-function-declaration` on older compilers, and always a link error.
- A signal handler must not call uBroker (mutexes are not async-signal-safe). Post a semaphore from the handler and publish from a thread:

```c
static void on_signal(int sig) { (void)sig; (void)sem_post(&s_ready); }      /* handler: wake only */
static void *imu_thread(void *p) { for (;;) { (void)sem_wait(&s_ready); (void)ubk_publish(...); } }
```

## Cautions

- **Packed structs:** messages use `#pragma pack(push, 1)`. Do not pass the address of a member (`&msg.field`) as a pointer: unaligned access can cause a HardFault (all Cortex-M0, and `LDRD`/FPU loads on M3 and above). Copy members by value.
- **Bitfield layout** is still chosen by the compiler. Do not send structs as-is over a link or store them in files.
- **Type check:** the API takes `void *` plus `size`, so only the size is checked. Passing a different message type of the same size is not detected.
- **One cursor per topic:** a `ubk_sub_t` used with a second topic reports `UBK_EVT_ERR_TOPIC`.

## Limits

| Item | Description |
|---|---|
| Blocking time | Each publish/subscribe copies the whole message while the topic is locked (FreeRTOS: interrupts masked). Measure on the target before using large messages (hundreds of bytes or more). |
| Many subscribers | Broker memory and publish cost are O(1), but every subscribe call takes the lock, and each subscriber keeps its own copy of the message. |
| Priority inversion (POSIX) | Without `UBK_CFG_POSIX_PRIO_INHERIT=1`, a low-priority thread holding a topic mutex can delay a high-priority one. |
| Stale data | Not detected. If a publisher stops, subscribers keep the last value. Use a `timestamp` field if needed. |
| Lost count | Inaccurate after missing 2^32 - 1 or more messages (negligible in practice). |
| Multi-core | FreeRTOS: not supported. POSIX: supported. |
| Variable-length fields | Not supported (fixed-length arrays only, like uORB). |
