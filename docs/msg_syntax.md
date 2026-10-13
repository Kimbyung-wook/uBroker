# Message Definition Syntax (.msg)

English | [한국어](msg_syntax_kr.md)

The syntax follows PX4 uORB, with a few extensions.

## Summary

```
# The leading comment block becomes the message description
TYPE  NAME = VALUE                 # constant (NAME in upper case)
TYPE  name                         # field (default 0)
TYPE  name  DEFAULT                # field + default (ROS2 syntax)
TYPE[N]  name  [v1, v2, ...]       # fixed-length array (+ default)
uint8 name : BITS  [DEFAULT]       # bitfield (extension)
msgname  name                      # nested message
# TOPICS topic_a topic_b           # several topics with one struct (uORB)
# TYPE_ONLY                        # type only, no topic (extension)
```

- A `#` after a field becomes the field comment in the generated header.
- Generated struct: `<msg>_t`, packed with `#pragma pack(push, 1)`.

## Types

| `.msg` type | C type | Notes |
|---|---|---|
| `bool` | `bool` | default `true` / `false` |
| `char` | `char` | 0 ~ 127 |
| `byte`, `uint8` / `int8` | `uint8_t` / `int8_t` | |
| `uint16` / `int16` | `uint16_t` / `int16_t` | |
| `uint32` / `int32` | `uint32_t` / `int32_t` | |
| `uint64` / `int64` | `uint64_t` / `int64_t` | |
| `float32` / `float64` | `float` / `double` | NaN/Inf defaults not allowed |
| `TickType_t`, `BaseType_t`, `UBaseType_t`, `StackType_t`, `EventBits_t` | same | FreeRTOS types. Size depends on the port: no bitfields, sign-only range check |
| another `.msg` name | `<name>_t` | nested message. Its default is the nested message's default |

## Constants

```
uint8 ARM_STATE_SAFE  = 0
uint8 ARM_STATE_ARMED = 1
```

- Generated as `#define <MSG>_<NAME>`, e.g. `WEAPON_STATE_ARM_STATE_ARMED`.
- The type must be a builtin type. `NAME` must be upper case.

## Defaults (ROS2 syntax)

- Write the value after the name, separated by spaces. With `=` the line is read as a constant.
- Formats: integer (decimal, `0x` hex), real, `true`/`false`, array `[a, b, c]` (count must equal the length).
- Values outside the type range or bit width are errors.
- Nested message fields cannot have a default.
- Every field without a default is initialized to 0 (`<MSG>_INITIALIZER`).

## Bitfields (extension)

- Allowed only on `uint8`, `uint16`, `uint32`. Not on arrays, `bool`, or FreeRTOS types.
- **Fill check:** consecutive bitfields of the same type must exactly fill the storage unit (8/16/32 bits). Declare unused bits as a `reserved` field.

```
uint8 is_calibrated : 1
uint8 error_code    : 5
uint8 reserved      : 2      # without this line: error (6/8 bits)
```

## Topics

- No directive: one topic named after the message (`UBK_TOPIC_<MSG>`).
- `# TOPICS a b`: one struct, topics `UBK_TOPIC_A`, `UBK_TOPIC_B` (multi-instance).
- `# TYPE_ONLY`: for messages used only as field types (e.g. `vector3`). Generates the struct, constants and initializer only. No topic ID, shared memory or table entry. Cannot be combined with `# TOPICS`.

## Naming Rules

- The message name comes from the file name. `weapon_state.msg` and `WeaponState.msg` both give `weapon_state`.
- Message and topic names must be lower `snake_case`. Topic names must be unique across all messages.
- Field names start with a letter, are at most 31 characters, and must not be C keywords or start with `ubk_`.
- Reserved message names: `ubk_msgs`, `ubk_types`, `ubroker`, and names starting with `ubk_`.
- Reserved topic names: `invalid`, `count` (clash with `UBK_TOPIC_INVALID`, `UBK_TOPIC_COUNT`).
- Generated variable names (`s_<topic>_data`, `s_<msg>_default`) must be at most 31 characters, which limits topic names to about 24 and message names to about 21 characters.

## Example

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
TickType_t  last_change_tick        # FreeRTOS tick of the last state change
```

More examples: [`test/msg/`](../test/msg)
