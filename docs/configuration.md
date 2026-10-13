# Configuration and Limits

English | [한국어](configuration_kr.md)

## Requirements

- FreeRTOS (single core)
- C99 compiler
- Python 3.6 or later (generator only)

## Configuration Macros

Define these before including `ubroker.h`, typically with a compiler `-D` option.

| Macro | Default | Description |
|---|---|---|
| `UBK_CFG_CHECK_PUBLISHER_TASK` | `0` | `1`: `ubk_publish` from any task other than the one that advertised returns `UBK_ERR_WRONG_TASK`. Requires `INCLUDE_xTaskGetCurrentTaskHandle = 1`. A topic advertised before the scheduler starts (ISR publisher) is not checked |

## FreeRTOS Requirements

- Concurrency uses FreeRTOS critical sections (`taskENTER_CRITICAL` / `taskENTER_CRITICAL_FROM_ISR`), not mutexes.
- An ISR that calls uBroker must have a priority at or below `configMAX_SYSCALL_INTERRUPT_PRIORITY`.
- Call `ubk_init_all()` once before any publish, usually in `main()` before the scheduler starts.

## Cautions

- **Packed structs:** messages use `#pragma pack(push, 1)`. Do not pass the address of a member (`&msg.field`) as a pointer: unaligned access can cause a HardFault (all Cortex-M0, and `LDRD`/FPU loads on M3 and above). Copy members by value.
- **Bitfield layout** is still chosen by the compiler. Do not send structs as-is over a link or store them in files.
- **Type check:** the API takes `void *` plus `size`, so only the size is checked. Passing a different message type of the same size is not detected. Match the topic ID and the variable type yourself.
- **One cursor per topic:** a `ubk_sub_t` used with a second topic reports `UBK_EVT_ERR_TOPIC`.

## Limits

| Item | Description |
|---|---|
| Interrupt-disabled time | Each publish/subscribe copies the whole message inside a critical section, so the time grows with message size. Measure on the target before using large messages (hundreds of bytes or more). |
| Many subscribers | Broker memory and publish cost are O(1), but every subscribe call enters a critical section, and each subscriber keeps its own copy of the message. |
| Stale data | Not detected. If a publisher stops, subscribers keep the last value. Use a `timestamp` field if needed. |
| Lost count | Inaccurate after missing 2^32 - 1 or more messages (negligible in practice). |
| Multi-core (SMP) | Not supported. |
| Variable-length fields | Not supported (fixed-length arrays only, like uORB). |
