#!/usr/bin/env python3
"""
uBroker message code generator.

Reads .msg files (uORB syntax + extensions) and generates C headers/sources for FreeRTOS.

Usage:
    python ubroker_gen.py -i <msg dir> -o <output dir> [--os freertos|posix] [--lang en|ko]

Outputs:
    <msg>.h                  per-message struct, constants, initializer
    ubk_msgs.h / ubk_msgs.c  topic IDs, defaults, shared memory, topic info table

Fixed files (core/, not generated):
    ubk_types.h, ubroker.h, ubroker.c

Generated code follows the DAPA SCR-G (weapon system software coding rules) naming rules and chapter 3 rules.
"""
import argparse
import os
import re
import sys
import zlib

GENERATOR_NAME = "ubroker_gen.py"
API_PREFIX = "ubk"
RESERVED_PREFIX = "ubk_"        # prefix not allowed in user field names
MAX_VAR_NAME = 31               # SCR-G: variable names up to 31 chars
LANGS = ("en", "ko")
LANG = "en"                     # message language, set by --lang
TARGET_OSES = {"freertos": "UBK_PORT_FREERTOS", "posix": "UBK_PORT_POSIX"}
TARGET_OS = "freertos"          # target OS, set by --os (must match UBK_CFG_PORT)


def tr(en, ko):
    """Pick the user-facing message for the current language."""
    return ko if LANG == "ko" else en


def pick_lang(args):
    """Read --lang before argparse so that --help is also translated."""
    lang = "en"
    for i, a in enumerate(args):
        if a == "--lang" and i + 1 < len(args):
            lang = args[i + 1]
        elif a.startswith("--lang="):
            lang = a.split("=", 1)[1]
    return lang if lang in LANGS else "en"


# ---------------------------------------------------------------------------
# Type definitions
# ---------------------------------------------------------------------------
class TypeInfo:
    """Builtin type info. kind: bool | uint | int | float | char"""

    def __init__(self, c_type, kind, bits, freertos=False):
        self.c_type = c_type
        self.kind = kind
        self.bits = bits            # None: platform-dependent size (FreeRTOS types)
        self.freertos = freertos


BUILTIN_TYPES = {
    # uORB builtin types
    "bool":    TypeInfo("bool", "bool", 8),
    "char":    TypeInfo("char", "char", 8),
    "byte":    TypeInfo("uint8_t", "uint", 8),
    "int8":    TypeInfo("int8_t", "int", 8),
    "int16":   TypeInfo("int16_t", "int", 16),
    "int32":   TypeInfo("int32_t", "int", 32),
    "int64":   TypeInfo("int64_t", "int", 64),
    "uint8":   TypeInfo("uint8_t", "uint", 8),
    "uint16":  TypeInfo("uint16_t", "uint", 16),
    "uint32":  TypeInfo("uint32_t", "uint", 32),
    "uint64":  TypeInfo("uint64_t", "uint", 64),
    "float32": TypeInfo("float", "float", 32),
    "float64": TypeInfo("double", "float", 64),
    # FreeRTOS types (size depends on FreeRTOSConfig.h / portmacro.h)
    "TickType_t":  TypeInfo("TickType_t", "uint", None, freertos=True),
    "BaseType_t":  TypeInfo("BaseType_t", "int", None, freertos=True),
    "UBaseType_t": TypeInfo("UBaseType_t", "uint", None, freertos=True),
    "StackType_t": TypeInfo("StackType_t", "uint", None, freertos=True),
    "EventBits_t": TypeInfo("EventBits_t", "uint", None, freertos=True),
}

# Types allowed for bitfields (fixed-width unsigned integers only)
BITFIELD_TYPES = ("uint8", "uint16", "uint32")

C_KEYWORDS = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "inline", "int", "long", "register", "restrict", "return", "short",
    "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
    "unsigned", "void", "volatile", "while", "bool", "true", "false",
    "_Bool", "_Complex", "_Imaginary",
}

# ---------------------------------------------------------------------------
# Regular expressions for parsing
# ---------------------------------------------------------------------------
# Constant (ROS2 / uORB): TYPE NAME = VALUE   (NAME in upper case)
RE_CONST = re.compile(r"^(\w+)\s+([A-Z][A-Z0-9_]*)\s*=\s*(.+)$")
# Field: TYPE[N] name [: BITS] [DEFAULT]
RE_FIELD = re.compile(
    r"^(\w+)(?:\[(\d+)\])?\s+([A-Za-z_][A-Za-z0-9_]*)"
    r"(?:\s*:\s*(\d+))?(?:\s+(.+))?$"
)
RE_TOPICS = re.compile(r"^#\s*TOPICS\s+(.+)$")
RE_TYPE_ONLY = re.compile(r"^#\s*TYPE_ONLY\s*$")
RE_QUEUE = re.compile(r"^#\s*QUEUE\s+(\S+)\s*$")
MAX_QUEUE_LEN = 65536           # QUEUE N: power of 2, 1 ~ 65536
RE_SNAKE = re.compile(r"^[a-z][a-z0-9_]*$")


class GenError(Exception):
    pass


class Field:
    def __init__(self, type_name, array_len, name, bits, default, comment, line):
        self.type_name = type_name
        self.array_len = array_len      # None: scalar
        self.name = name
        self.bits = bits                # None: not a bitfield
        self.default = default          # raw text or None
        self.comment = comment
        self.line = line
        self.nested = None              # Message object if nested


class Constant:
    def __init__(self, type_name, name, value, comment, line):
        self.type_name = type_name
        self.name = name
        self.value = value
        self.comment = comment
        self.line = line


class Message:
    def __init__(self, name, path):
        self.name = name
        self.path = path
        self.description = []
        self.fields = []
        self.constants = []
        self.topics = []
        self.type_only = False          # True: type only (no topic)
        self.queue_len = 1              # # QUEUE N (1: mailbox)
        self.queue_line = None          # where # QUEUE was given

    @property
    def c_type(self):
        return f"{self.name}_t"

    @property
    def upper(self):
        return self.name.upper()


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------
def camel_to_snake(name):
    """SensorImu -> sensor_imu (supports the new PX4 file naming). snake_case is kept as is."""
    s = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s)
    return s.lower()


def clean_comment(text):
    """Sanitize text so it can be placed inside a C block comment."""
    return text.strip().replace("*/", "* /").replace("/*", "/ *")


def parse_queue(msg, text, where, errors):
    """# QUEUE N: N must be a power of 2 so slot order survives sequence wrap-around."""
    if msg.queue_line is not None:
        errors.append(f"{where}: error: " + tr(f"QUEUE is already given at {msg.queue_line}",
                                                f"QUEUE 가 이미 {msg.queue_line} 에 있습니다"))
        return
    msg.queue_line = where
    try:
        n = int(text, 10)
    except ValueError:
        n = 0
    if n < 1 or n > MAX_QUEUE_LEN or (n & (n - 1)) != 0:
        errors.append(f"{where}: error: " + tr(
            f"QUEUE length must be a power of 2 between 1 and {MAX_QUEUE_LEN}: '{text}'",
            f"QUEUE 길이는 1 ~ {MAX_QUEUE_LEN} 사이의 2의 거듭제곱이어야 합니다: '{text}'"))
        return
    msg.queue_len = n


def parse_msg_file(path, errors):
    base = os.path.splitext(os.path.basename(path))[0]
    msg = Message(camel_to_snake(base), path)
    if not RE_SNAKE.match(msg.name):
        errors.append(f"{path}: error: " + tr(f"message name '{msg.name}' must be lower snake_case",
                                                 f"메시지 이름 '{msg.name}' 은 소문자 snake_case 여야 합니다"))

    seen_body = False
    with open(path, "r", encoding="utf-8") as f:
        for line_num, raw in enumerate(f, 1):
            where = f"{path}:{line_num}"
            stripped = raw.strip()
            if not stripped:
                continue

            m = RE_TOPICS.match(stripped)
            if m:
                msg.topics.extend(m.group(1).split())
                continue

            if RE_TYPE_ONLY.match(stripped):
                msg.type_only = True
                continue

            m = RE_QUEUE.match(stripped)
            if m:
                parse_queue(msg, m.group(1), where, errors)
                continue

            if stripped.startswith("#"):
                if not seen_body:
                    msg.description.append(clean_comment(stripped[1:]))
                continue

            seen_body = True
            body, _, comment = stripped.partition("#")
            body = body.strip()
            comment = clean_comment(comment)

            m = RE_CONST.match(body)
            if m:
                msg.constants.append(Constant(m.group(1), m.group(2), m.group(3).strip(),
                                              comment, where))
                continue

            m = RE_FIELD.match(body)
            if m:
                t, n, name, bits, default = m.groups()
                msg.fields.append(Field(
                    t,
                    int(n) if n is not None else None,
                    name,
                    int(bits) if bits is not None else None,
                    default.strip() if default is not None else None,
                    comment,
                    where,
                ))
                continue

            errors.append(f"{where}: error: " + tr(f"cannot parse: {body}", f"구문 분석 실패: {body}"))

    if msg.type_only and msg.topics:
        errors.append(f"{path}: error: " + tr("TYPE_ONLY and TOPICS cannot be used together",
                                                 "TYPE_ONLY 와 TOPICS 는 함께 쓸 수 없습니다"))
    elif msg.type_only and msg.queue_line is not None:
        errors.append(f"{msg.queue_line}: error: " + tr("TYPE_ONLY and QUEUE cannot be used together",
                                                         "TYPE_ONLY 와 QUEUE 는 함께 쓸 수 없습니다"))
    elif not msg.topics and not msg.type_only:
        msg.topics = [msg.name]
    return msg


# ---------------------------------------------------------------------------
# Value conversion / validation
# ---------------------------------------------------------------------------
def parse_int(text):
    try:
        return int(text, 0)
    except ValueError:
        raise GenError(tr(f"not an integer: '{text}'", f"정수 값이 아닙니다: '{text}'"))


def int_range(info):
    if info.bits is None:
        return None
    if info.kind == "uint":
        return 0, (1 << info.bits) - 1
    return -(1 << (info.bits - 1)), (1 << (info.bits - 1)) - 1


def scalar_literal(info, text, bits=None):
    """Convert a builtin-type value string to a C literal (with range check)."""
    if info.kind == "bool":
        if text not in ("true", "false"):
            raise GenError(tr(f"bool value must be true/false: '{text}'", f"bool 값은 true/false 여야 합니다: '{text}'"))
        return text

    if info.kind == "float":
        try:
            value = float(text)
        except ValueError:
            raise GenError(tr(f"not a real number: '{text}'", f"실수 값이 아닙니다: '{text}'"))
        if value != value or value in (float("inf"), float("-inf")):
            raise GenError(tr(f"NaN/Inf is not allowed: '{text}'", f"NaN/Inf 는 허용되지 않습니다: '{text}'"))
        lit = repr(value)
        if "." not in lit and "e" not in lit:
            lit += ".0"
        return lit + ("F" if info.bits == 32 else "")

    # Integer kinds (uint, int, char)
    value = parse_int(text)
    if bits is not None:
        lo, hi = 0, (1 << bits) - 1
    elif info.kind == "char":
        lo, hi = 0, 127
    else:
        rng = int_range(info)
        if rng is None:                     # FreeRTOS types: sign check only
            lo, hi = (0, None) if info.kind == "uint" else (None, None)
        else:
            lo, hi = rng
    if (lo is not None and value < lo) or (hi is not None and value > hi):
        raise GenError(tr(f"value {value} is out of range ({lo} ~ {hi})",
                          f"값 {value} 가 허용 범위({lo} ~ {hi})를 벗어납니다"))

    is_hex = text.lower().startswith("0x")
    digits = text if is_hex else str(value)
    if info.freertos or info.kind == "char":
        suffix = "U" if info.kind == "uint" and value >= 0 else ""
        return f"(({info.c_type}){digits}{suffix})"
    if info.kind == "uint":
        return digits + ("ULL" if info.bits == 64 else "U")
    if info.bits == 64:
        return digits + "LL"
    return digits


def zero_text(info):
    if info.kind == "bool":
        return "false"
    if info.kind == "float":
        return "0.0"
    return "0"


def split_array_default(text):
    text = text.strip()
    if not (text.startswith("[") and text.endswith("]")):
        raise GenError(tr(f"array default must be in [a, b, ...] form: '{text}'",
                          f"배열 기본값은 [a, b, ...] 형식이어야 합니다: '{text}'"))
    inner = text[1:-1].strip()
    return [v.strip() for v in inner.split(",")] if inner else []


# ---------------------------------------------------------------------------
# Semantic checks
# ---------------------------------------------------------------------------
def freertos_type_error(type_name):
    return tr(f"FreeRTOS type '{type_name}' is not available with --os {TARGET_OS}",
              f"FreeRTOS 타입 '{type_name}' 은 --os {TARGET_OS} 에서 쓸 수 없습니다")


def validate(messages, errors):
    by_name = {m.name: m for m in messages}

    # Global duplicate check for topic names
    topic_owner = {}
    for msg in messages:
        for t in msg.topics:
            if not RE_SNAKE.match(t):
                errors.append(f"{msg.path}: error: " + tr(f"topic name '{t}' must be lower snake_case",
                                                         f"토픽 이름 '{t}' 은 소문자 snake_case 여야 합니다"))
            if t in topic_owner:
                errors.append(f"{msg.path}: error: " + tr(f"topic '{t}' is also defined in {topic_owner[t]}",
                                                         f"토픽 '{t}' 이 {topic_owner[t]} 에도 정의되어 있습니다"))
            topic_owner[t] = msg.path
            if t != msg.name and t in by_name:
                errors.append(f"{msg.path}: error: " + tr(f"topic '{t}' clashes with another message name",
                                                         f"토픽 '{t}' 이 다른 메시지 이름과 충돌합니다"))

    for msg in messages:
        if not msg.fields:
            errors.append(f"{msg.path}: error: " + tr("no fields (empty structs are not allowed in C)",
                                                     "필드가 하나도 없습니다 (빈 구조체는 C 에서 허용되지 않음)"))
        names = set()

        for c in msg.constants:
            info = BUILTIN_TYPES.get(c.type_name)
            if info is None:
                errors.append(f"{c.line}: error: " + tr(f"constant type must be a builtin type: '{c.type_name}'",
                                                       f"상수 타입은 기본 타입이어야 합니다: '{c.type_name}'"))
                continue
            if info.freertos and TARGET_OS != "freertos":
                errors.append(f"{c.line}: error: " + freertos_type_error(c.type_name))
                continue
            if c.name in names:
                errors.append(f"{c.line}: error: " + tr(f"duplicate name: '{c.name}'", f"이름 중복: '{c.name}'"))
            names.add(c.name)
            try:
                c.literal = scalar_literal(info, c.value)
            except GenError as e:
                errors.append(f"{c.line}: error: " + tr(f"constant {c.name}: {e}", f"상수 {c.name}: {e}"))

        for fld in msg.fields:
            if not fld.name[0].isalpha():
                errors.append(f"{fld.line}: error: " + tr(f"field name must start with a letter (SCR-G): '{fld.name}'",
                                                         f"필드 이름의 첫 글자는 문자여야 합니다 (SCR-G): '{fld.name}'"))
            if fld.name.startswith(RESERVED_PREFIX):
                errors.append(f"{fld.line}: error: " + tr(
                    f"field names starting with '{RESERVED_PREFIX}' are reserved for the broker: '{fld.name}'",
                    f"'{RESERVED_PREFIX}' 로 시작하는 필드 이름은 브로커 내부용으로 예약되어 있습니다: '{fld.name}'"))
            if len(fld.name) > MAX_VAR_NAME:
                errors.append(f"{fld.line}: error: " + tr(
                    f"field name must be at most {MAX_VAR_NAME} chars (SCR-G): '{fld.name}'",
                    f"필드 이름은 {MAX_VAR_NAME}자 이하여야 합니다 (SCR-G): '{fld.name}'"))
            if fld.name in C_KEYWORDS:
                errors.append(f"{fld.line}: error: " + tr(f"C keyword cannot be a field name: '{fld.name}'",
                                                         f"C 예약어는 필드 이름으로 쓸 수 없습니다: '{fld.name}'"))
            if fld.name in names:
                errors.append(f"{fld.line}: error: " + tr(f"duplicate name: '{fld.name}'", f"이름 중복: '{fld.name}'"))
            names.add(fld.name)

            if fld.array_len is not None and fld.array_len < 1:
                errors.append(f"{fld.line}: error: " + tr("array length must be at least 1", "배열 길이는 1 이상이어야 합니다"))

            if fld.type_name in BUILTIN_TYPES:
                info = BUILTIN_TYPES[fld.type_name]
                if info.freertos and TARGET_OS != "freertos":
                    errors.append(f"{fld.line}: error: " + freertos_type_error(fld.type_name))
                    continue
            elif fld.type_name in by_name:
                fld.nested = by_name[fld.type_name]
                info = None
                if fld.bits is not None:
                    errors.append(f"{fld.line}: error: " + tr("nested messages cannot be bitfields",
                                                             "중첩 메시지에는 비트필드를 쓸 수 없습니다"))
                if fld.default is not None:
                    errors.append(f"{fld.line}: error: " + tr(
                        "nested message fields cannot have a default (the nested message default is used)",
                        "중첩 메시지 필드에는 기본값을 지정할 수 없습니다 (중첩 메시지의 기본값이 사용됩니다)"))
                continue
            else:
                errors.append(f"{fld.line}: error: " + tr(f"unknown type: '{fld.type_name}'", f"알 수 없는 타입: '{fld.type_name}'"))
                continue

            if fld.bits is not None:
                if fld.type_name not in BITFIELD_TYPES:
                    errors.append(f"{fld.line}: error: " + tr(f"bitfields are allowed only for {', '.join(BITFIELD_TYPES)}",
                                                             f"비트필드는 {', '.join(BITFIELD_TYPES)} 타입만 허용됩니다"))
                    continue
                if fld.array_len is not None:
                    errors.append(f"{fld.line}: error: " + tr("arrays cannot be bitfields", "배열에는 비트필드를 쓸 수 없습니다"))
                    continue
                if not 1 <= fld.bits <= info.bits:
                    errors.append(f"{fld.line}: error: " + tr(f"bit width must be 1 ~ {info.bits}", f"비트 폭은 1 ~ {info.bits} 이어야 합니다"))
                    continue

            if fld.default is not None:
                try:
                    if fld.array_len is None:
                        scalar_literal(info, fld.default, fld.bits)
                    else:
                        values = split_array_default(fld.default)
                        if len(values) != fld.array_len:
                            raise GenError(tr(
                                f"array default count ({len(values)}) differs from length ({fld.array_len})",
                                f"배열 기본값 개수({len(values)})가 길이({fld.array_len})와 다릅니다"))
                        for v in values:
                            scalar_literal(info, v)
                except GenError as e:
                    errors.append(f"{fld.line}: error: " + tr(f"field {fld.name}: {e}", f"필드 {fld.name}: {e}"))

        check_bitfields(msg, errors)

    check_cycles(messages, errors)
    check_names(messages, errors)


def check_bitfields(msg, errors):
    """Check that consecutive bitfields of the same type exactly fill the storage unit."""
    group = None    # [type_name, used_bits, width, first_field]

    def unfilled(g):
        return f"{g[3].line}: error: " + tr(
            f"bitfield group ({g[3].name}~) does not fill its {g[0]} storage unit "
            f"({g[1]}/{g[2]} bits used). Declare the remaining bits as a reserved field",
            f"비트필드 그룹({g[3].name}~)이 {g[0]} 저장 단위를 "
            f"채우지 않습니다 ({g[1]}/{g[2]} bit 사용). 남는 비트는 reserved 필드로 명시하세요")

    for fld in msg.fields:
        if fld.bits is None or fld.type_name not in BITFIELD_TYPES or fld.array_len is not None:
            if group is not None:
                errors.append(unfilled(group))
                group = None
            continue
        width = BUILTIN_TYPES[fld.type_name].bits
        if group is not None and (group[0] != fld.type_name or group[1] + fld.bits > width):
            errors.append(unfilled(group))
            group = None
        if group is None:
            group = [fld.type_name, 0, width, fld]
        group[1] += fld.bits
        if group[1] == width:
            group = None
    if group is not None:
        errors.append(unfilled(group))


def check_cycles(messages, errors):
    state = {}

    def visit(msg, stack):
        if state.get(msg.name) == "done":
            return
        if state.get(msg.name) == "visiting":
            errors.append(f"{msg.path}: error: " + tr("circular nested message reference: ", "중첩 메시지 순환 참조: ")
                          + " -> ".join(stack + [msg.name]))
            return
        state[msg.name] = "visiting"
        for fld in msg.fields:
            if fld.nested is not None:
                visit(fld.nested, stack + [msg.name])
        state[msg.name] = "done"

    for m in messages:
        visit(m, [])



# ---------------------------------------------------------------------------
# Generated identifier names
#   SCR-G ch.2 2.da : variable names <= 31 chars, static s_, pointer p_, count n_
#   SCR-G ch.2 2.ra : macros/enumerators in upper case
# ---------------------------------------------------------------------------
def topic_enum(topic):
    return f"UBK_TOPIC_{topic.upper()}"


TOPIC_COUNT_ENUM = "UBK_TOPIC_COUNT"
TOPIC_INVALID_ENUM = "UBK_TOPIC_INVALID"
RESERVED_FILE_NAMES = ("ubk_msgs", "ubk_types", "ubroker")   # generated/fixed file names


def static_var_names(msg):
    names = [f"s_{msg.name}_default"] if msg.topics else []
    for t in msg.topics:
        names.append(f"s_{t}_data")
    return names


def check_names(messages, errors):
    """Check topic ID enumerator clashes and generated variable name lengths."""
    for msg in messages:
        if msg.name in RESERVED_FILE_NAMES or msg.name.startswith(RESERVED_PREFIX):
            errors.append(f"{msg.path}: error: " + tr(f"message name '{msg.name}' is reserved (clashes with a uBroker file)",
                                                     f"메시지 이름 '{msg.name}' 은 예약되어 있습니다 (uBroker 파일과 충돌)"))
        for t in msg.topics:
            if topic_enum(t) in (TOPIC_COUNT_ENUM, TOPIC_INVALID_ENUM):
                errors.append(f"{msg.path}: error: " + tr(f"topic name '{t}' is reserved (clashes with {topic_enum(t)})",
                                                         f"토픽 이름 '{t}' 은 예약어입니다 ({topic_enum(t)} 와 충돌)"))
        for v in static_var_names(msg):
            if len(v) > MAX_VAR_NAME:
                errors.append(f"{msg.path}: error: " + tr(
                    f"generated variable name '{v}' exceeds {MAX_VAR_NAME} chars. "
                    f"Shorten the message/topic name (SCR-G)",
                    f"생성 변수 이름 '{v}' 가 {MAX_VAR_NAME}자를 넘습니다. 메시지/토픽 이름을 줄이세요 (SCR-G)"))


# ---------------------------------------------------------------------------
# Code generation
# ---------------------------------------------------------------------------
def banner(source):
    src = f" from {source}" if source is not None else ""
    return f"/* Auto-generated by {GENERATOR_NAME}{src}. DO NOT EDIT. */\n"


def field_initializer(fld):
    """Designated initializer value of one field (0 if no default)."""
    if fld.nested is not None:
        one = f"{fld.nested.upper}_INITIALIZER"
        if fld.array_len is None:
            return one
        return "{ " + ", ".join([one] * fld.array_len) + " }"

    info = BUILTIN_TYPES[fld.type_name]
    if fld.array_len is None:
        text = fld.default if fld.default is not None else zero_text(info)
        return scalar_literal(info, text, fld.bits)

    if fld.default is not None:
        values = split_array_default(fld.default)
    else:
        values = [zero_text(info)] * fld.array_len
    return "{ " + ", ".join(scalar_literal(info, v) for v in values) + " }"


def gen_msg_header(msg):
    guard = f"UBK_{msg.upper}_H"
    uses_freertos = any(f.nested is None and BUILTIN_TYPES[f.type_name].freertos for f in msg.fields)
    nested = sorted({f.nested.name for f in msg.fields if f.nested is not None})
    desc = " ".join(d for d in msg.description if d) or msg.name

    out = [banner(os.path.basename(msg.path))]
    out.append(f"\n#ifndef {guard}\n#define {guard}\n\n")
    out.append('#include "ubk_types.h"\n')
    if uses_freertos:
        out.append('#include "FreeRTOS.h"\n')
    for n in nested:
        out.append(f'#include "{n}.h"\n')
    out.append('\n#ifdef __cplusplus\nextern "C" {\n#endif\n\n')

    if msg.constants:
        for c in msg.constants:
            info = BUILTIN_TYPES[c.type_name]
            if info.kind in ("bool", "float"):
                value = f"({c.literal})"
            elif info.freertos or info.kind == "char":
                value = c.literal               # already in ((type)v) form
            else:
                value = f"(({info.c_type}){c.literal})"
            cmt = f"  /* {c.comment} */" if c.comment else ""
            out.append(f"#define {msg.upper}_{c.name} {value}{cmt}\n")
        out.append("\n")

    topics = ", ".join(topic_enum(t) for t in msg.topics) or "none (TYPE_ONLY)"
    if msg.topics and msg.queue_len > 1:
        topics += f" (queue of {msg.queue_len}, FIFO)"
    out.append(f"/* {desc}\n * - topics: {topics}\n"
               f" * - pack(1): do not pass member addresses (&msg.field) as pointers */\n")
    out.append("#pragma pack(push, 1)\ntypedef struct\n{\n")
    for fld in msg.fields:
        c_type = fld.nested.c_type if fld.nested is not None else BUILTIN_TYPES[fld.type_name].c_type
        decl = f"{c_type} {fld.name}"
        if fld.array_len is not None:
            decl += f"[{fld.array_len}]"
        if fld.bits is not None:
            decl += f" : {fld.bits}"
        decl += ";"
        cmt = f" /* {fld.comment} */" if fld.comment else ""
        out.append(f"    {decl:<40}{cmt}".rstrip() + "\n")
    out.append(f"}} {msg.c_type};\n#pragma pack(pop)\n\n")

    out.append(f"#define {msg.upper}_INITIALIZER \\\n    {{ \\\n")
    inits = [f"        .{fld.name} = {field_initializer(fld)}" for fld in msg.fields]
    out.append(", \\\n".join(inits) + " \\\n    }\n")

    out.append('\n#ifdef __cplusplus\n}\n#endif\n\n')
    out.append(f"#endif /* {guard} */\n")
    return "".join(out)


def topic_key(topic):
    return (zlib.crc32(topic.encode("utf-8")) | 1) & 0xFFFFFFFF


def all_topics(messages):
    """List of (message, topic). This order is the topic ID order."""
    return [(m, t) for m in messages for t in m.topics]


MSGS_NAME = "ubk_msgs"


def gen_msgs_header(messages):
    out = [banner(None), "\n#ifndef UBK_MSGS_H\n#define UBK_MSGS_H\n\n", '#include "ubk_types.h"\n']
    for m in messages:
        out.append(f'#include "{m.name}.h"\n')
    out.append('\n#ifdef __cplusplus\nextern "C" {\n#endif\n\n')

    out.append(f"/* Target OS of this generated code (ubroker_gen.py --os {TARGET_OS}), "
               f"checked against UBK_CFG_PORT by ubroker.c */\n")
    out.append(f"#define UBK_GEN_OS {TARGET_OSES[TARGET_OS]}\n\n")

    out.append("/* Topic IDs (0 is the init value, so it is invalid) */\ntypedef enum\n{\n")
    out.append(f"    {TOPIC_INVALID_ENUM} = 0,\n")
    for m, t in all_topics(messages):
        out.append(f"    {topic_enum(t) + ',':<40} /* {m.c_type} */\n")
    out.append(f"    {TOPIC_COUNT_ENUM}\n}} ubk_topic_id_t;\n\n")

    out.append("/* Topic info table (internal, used by ubroker.c) */\n")
    out.append(f"extern const ubk_topic_meta_t g_ubk_topic_meta[{TOPIC_COUNT_ENUM}];\n")
    out.append('\n#ifdef __cplusplus\n}\n#endif\n\n#endif /* UBK_MSGS_H */\n')
    return "".join(out)


def gen_msgs_source(messages):
    topics = all_topics(messages)
    out = [banner(None), f'\n#include "{MSGS_NAME}.h"\n']

    out.append("\n/* ---- Message defaults ---- */\n\n")
    for m in messages:
        if m.topics:
            out.append(f"static const {m.c_type} s_{m.name}_default = {m.upper}_INITIALIZER;\n")

    out.append("\n/* ---- Topic shared memory ---- */\n\n")
    for m, t in topics:
        out.append(f"static {m.c_type} s_{t}_data[{m.queue_len}U] = {{ {m.upper}_INITIALIZER }};"
                   f"   /* ubk_init_topic fills every slot */\n")

    out.append(f"\nconst ubk_topic_meta_t g_ubk_topic_meta[{TOPIC_COUNT_ENUM}] =\n{{\n")
    rows = [f"    [{TOPIC_INVALID_ENUM}] = {{ NULL, NULL, 0U, 0U, UBK_PUB_INVALID }}"]
    rows += [f"    [{topic_enum(t)}] = {{ s_{t}_data, &s_{m.name}_default, sizeof({m.c_type}), {m.queue_len}U, "
             f"(ubk_pub_t)0x{topic_key(t):08X}U }}" for m, t in topics]
    out.append(",\n".join(rows) + "\n};\n")
    return "".join(out)


# ---------------------------------------------------------------------------
# Output (write only files whose content changed)
# ---------------------------------------------------------------------------
def write_if_changed(path, content):
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8", newline="") as f:
            if f.read() == content:
                return False
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)
    return True


# Delete only files starting with the generator banner (protects user files)
def remove_if_generated(path):
    if not os.path.isfile(path):
        return False
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        first = f.readline()
    if not first.startswith(f"/* Auto-generated by {GENERATOR_NAME}"):
        return False
    os.remove(path)
    return True


def main(argv=None):
    global LANG
    LANG = pick_lang(sys.argv[1:] if argv is None else argv)   # applies to --help as well

    ap = argparse.ArgumentParser(description=tr("uBroker message code generator", "uBroker 메시지 코드 생성기"))
    ap.add_argument("-i", "--input", required=True, help=tr("folder with .msg files", ".msg 파일이 있는 폴더"))
    ap.add_argument("-o", "--output", required=True, help=tr("folder for generated files", "생성 파일을 저장할 폴더"))
    ap.add_argument("--lang", choices=LANGS, default="en",
                    help=tr("message language (default: en)", "메시지 언어 (기본값: en)"))
    ap.add_argument("--os", dest="target_os", choices=tuple(TARGET_OSES), default="freertos",
                    help=tr("target OS, must match UBK_CFG_PORT (default: freertos)",
                            "대상 OS, UBK_CFG_PORT 와 같아야 함 (기본값: freertos)"))
    args = ap.parse_args(argv)

    global TARGET_OS
    TARGET_OS = args.target_os

    if not os.path.isdir(args.input):
        print("error: " + tr(f"input folder not found: {args.input}", f"입력 폴더가 없습니다: {args.input}"),
              file=sys.stderr)
        return 1

    msg_files = sorted(os.path.join(args.input, f) for f in os.listdir(args.input) if f.endswith(".msg"))
    if not msg_files:
        print("error: " + tr(f"no .msg files in: {args.input}", f".msg 파일이 없습니다: {args.input}"),
              file=sys.stderr)
        return 1

    errors = []
    messages = [parse_msg_file(p, errors) for p in msg_files]
    names = [m.name for m in messages]
    for n in set(names):
        if names.count(n) > 1:
            errors.append("error: " + tr(f"duplicate message name: '{n}'", f"메시지 이름 중복: '{n}'"))
    if not errors:
        validate(messages, errors)
    if errors:
        for e in errors:
            print(e, file=sys.stderr)
        print(f"{len(errors)} error(s). " + tr("Nothing was generated.", "생성하지 않았습니다."), file=sys.stderr)
        return 1

    os.makedirs(args.output, exist_ok=True)
    outputs = [(f"{m.name}.h", gen_msg_header(m)) for m in messages]
    outputs.append((f"{MSGS_NAME}.h", gen_msgs_header(messages)))
    outputs.append((f"{MSGS_NAME}.c", gen_msgs_source(messages)))

    for name, content in outputs:
        changed = write_if_changed(os.path.join(args.output, name), content)
        print(f"  {'updated  ' if changed else 'unchanged'} {name}")

    expected = {name for name, _ in outputs}
    for name in sorted(os.listdir(args.output)):
        if (name not in expected) and name.endswith((".h", ".c")):
            if remove_if_generated(os.path.join(args.output, name)):
                print(f"  removed   {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
