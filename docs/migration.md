# Migrating Existing Code to uBroker

English | [한국어](migration_kr.md)

This guide converts FreeRTOS code whose tasks share data through **global variables and FreeRTOS queues** to uBroker. A person does the conversion. The migration tools (`tools/migrate/`) do the repetitive parts. They need only Python 3 (standard library), so they can be copied into a closed network.

> The migration tools are distributed separately and are not included in this repository.

## Procedure

| Step | Person | Tool |
|---|---|---|
| 1. Inventory | review the list, add what is missing | `ipc_inventory.py` writes an IPC list (CSV) |
| 2. Classify | decide the conversion of each row (table below) | the CSV has a suggested type and reason |
| 3. Define messages | finish the `.msg` files, choose topic names | `struct2msg.py` turns a C struct into a `.msg` draft |
| 4. Generate | - | `ubroker_gen.py --os freertos` (validates the `.msg`) |
| 5. Convert | one topic at a time: publisher first, then subscribers, build after each | - |
| 6. Residual check | remove what is left, set `status` to `done` | `ipc_residual.py` lists remaining references |
| 7. Verify | test on the target (checklist below) | `UBK_CFG_CHECK_PUBLISHER_TASK=1` |

```
python tools/migrate/ipc_inventory.py <src> -o inventory.csv --exclude FreeRTOS-Kernel
python tools/migrate/struct2msg.py <src>/app_types.h sensor_data_t -o msg
python code_gen/ubroker_gen.py -i msg -o gen --os freertos
python tools/migrate/ipc_residual.py <src> inventory.csv
```

All tools take `--lang ko` for Korean messages.

## Classification

| Existing form | Conversion | `recommended` in the CSV |
|---|---|---|
| global variable, latest value is enough (state, sensor value) | mailbox topic | `mailbox` |
| queue whose every item matters (commands, events), polled | `# QUEUE N` topic, N = power of 2 >= old length | `queue` |
| queue a task blocks on (`xQueueReceive(q, &x, timeout)`) | `# QUEUE N` topic + `xTaskNotifyGive` after publish | `queue+notify` |
| global written by several tasks | one topic per writer, or one owner task that publishes | `split-or-owner` |
| data written by an ISR | advertise before the scheduler, `ubk_publish_from_isr` | `isr` column = `yes` |
| queue length 1 / `xQueueOverwrite` | mailbox topic | `mailbox` |
| work queue: several consumers **share** the items | keep the FreeRTOS queue (uBroker gives every message to every subscriber) | `keep` |
| queue used as a semaphore (item size 0) | keep | `keep` |
| variable used by one task only | not IPC, leave it | `skip` |
| written only in `main()` before the scheduler | constant/config: keep, or a topic published once | `review` |

Also keep a FreeRTOS queue, or pass a pointer/index instead, for **large buffers** (hundreds of bytes or more): uBroker copies the message inside a critical section (see Performance).

The notify pattern makes the publisher know the subscriber's task handle. Allow that coupling only for this case.

## Before / After

### Global variable -> mailbox topic

```c
/* before */
sensor_data_t g_sensor_data;                 /* sensor.c */
g_sensor_data = local;                       /* SensorTask */
if (g_sensor_data.is_valid != 0U) { }        /* LogTask */

/* after */
ubk_pub_t pub = UBK_PUB_INVALID;             /* SensorTask */
(void)ubk_advertise(UBK_TOPIC_SENSOR_DATA, &pub);
(void)ubk_publish(UBK_TOPIC_SENSOR_DATA, pub, &local, sizeof(local));

ubk_sub_t     sub    = UBK_SUB_INITIALIZER;  /* LogTask: one cursor per task and topic */
sensor_data_t sensor = SENSOR_DATA_INITIALIZER;
ubk_subscribe(UBK_TOPIC_SENSOR_DATA, &sub, &sensor, sizeof(sensor));
if (sensor.is_valid != 0U) { }               /* the previous value is kept when nothing is new */
```

### Blocking queue -> QUEUE topic + task notification

```c
/* before */
(void)xQueueSend(g_cmd_queue, &cmd, 0U);                         /* CommTask */
if (xQueueReceive(g_cmd_queue, &cmd, portMAX_DELAY) == pdPASS)   /* ControlTask */
{
    handle(&cmd);
}

/* after (cmd_msg.msg has '# QUEUE 8') */
if (ubk_publish(UBK_TOPIC_CMD_MSG, pub, &cmd, sizeof(cmd)) == UBK_OK)   /* CommTask */
{
    (void)xTaskNotifyGive(g_control_task);
}

(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);                   /* ControlTask */
is_draining = true;
while (is_draining == true)
{
    ubk_subscribe(UBK_TOPIC_CMD_MSG, &cmd_sub, &cmd, sizeof(cmd));
    if ((cmd_sub.event == UBK_EVT_NEW) || (cmd_sub.event == UBK_EVT_NEW_LOST))
    {
        handle(&cmd);                        /* oldest first */
    }
    else
    {
        is_draining = false;
    }
}
```

- Drain until `UBK_EVT_NO_NEW`: one notification can stand for several messages.
- `UBK_EVT_NEW_LOST` means the queue overflowed (`cmd_sub.n_lost` messages). The old queue made the sender fail or wait instead. Count it and size N so that it does not happen.

### ISR producer

```c
static ubk_pub_t s_rx_pub = UBK_PUB_INVALID;

void Uart_Init(void)                         /* before vTaskStartScheduler() */
{
    (void)ubk_advertise(UBK_TOPIC_UART_RX, &s_rx_pub);
}

void UART1_IRQHandler(void)
{
    (void)ubk_publish_from_isr(UBK_TOPIC_UART_RX, s_rx_pub, &rx, sizeof(rx));
}
```

### Several writers

```c
/* before: ControlTask and CommTask both write g_mode */

/* after, option A: one topic per writer, readers subscribe to both */
UBK_TOPIC_MODE_REQ_CONTROL, UBK_TOPIC_MODE_REQ_COMM      /* '# TOPICS mode_req_control mode_req_comm' */

/* after, option B: CommTask publishes a request, ControlTask owns and publishes the mode */
```

### Subscribe once per cycle

Code that read a global variable directly tends to read it wherever a field is needed. Do not replace each of those reads with a `ubk_subscribe` call: every call enters a critical section and copies the whole message. Subscribe once at the start of the task cycle and use the local copy for the rest of the cycle.

```c
/* avoid: one subscribe per field access */
ubk_subscribe(UBK_TOPIC_SENSOR_DATA, &sub, &s, sizeof(s)); a = s.accel.x;
ubk_subscribe(UBK_TOPIC_SENSOR_DATA, &sub, &s, sizeof(s)); b = s.accel.y;

/* recommended: one subscribe per cycle, then the local copy */
ubk_subscribe(UBK_TOPIC_SENSOR_DATA, &sub, &s, sizeof(s));
a = s.accel.x;
b = s.accel.y;
```

- The number of calls becomes "topics per cycle" instead of "field accesses".
- All fields used in one cycle come from the same message, so they are consistent with each other.
- Helper functions take the local copy as a parameter (`const sensor_data_t *p_s`) instead of reading the topic again.

## Tool Notes

### ipc_inventory.py
- Finds the following:
  - Task entry functions from `xTaskCreate*`.
  - ISRs: names ending in `IRQHandler` / `_ISR` / `Isr`, and functions that call `*FromISR`.
  - File-scope variables.
  - Queues created with `xQueueCreate*`, and every `xQueueSend*/Receive*/Peek/Overwrite` call.
- Accesses are tied to tasks through a simple call graph. `?(func)` means the function is not reached from any task, for example through a function pointer.
- CSV columns: `name, kind, type, defined_at, writers, readers, blocking, isr, recommended, queue_n, reason, status`. The CSV is UTF-8 with BOM, so Excel opens it directly.
- **Not seen:**
  - Accesses through macros, pointers (`p = &g_x`) and function pointers.
  - A local variable that shadows a global.
  - Two `static` variables with the same name in different files: they are merged.
- Exclude the kernel and vendor folders with `--exclude`.

### struct2msg.py
- Converts the types, arrays (literal or `#define` length), bitfields and nested structs (as `TYPE_ONLY` drafts). Field comments are copied.
- Bitfield runs are padded with a `reserved` field so that they fill the storage unit, as the generator requires.
- `int`, `long` and `short` are mapped to the usual 32-bit MCU sizes and reported. Enums are stored as the smallest unsigned type that fits.
- Pointers, unions, multi-dimensional arrays and unknown types become `# TODO` lines. When any TODO is left, the exit code is 2.
- **uBroker structs are packed** (`#pragma pack(1)`). Check code that depends on the old layout: raw byte copies, DMA, binary logs.

### ipc_residual.py
- Checks the rows whose `status` is `done` (or `--status <value>`). Comments and strings are ignored.
- Exit code 0: nothing left, 1: references left. It can be used as a build gate.

### topic_graph.py
- Draws the topic graph (also called a pub/sub or computation graph) of code that already uses uBroker:
  - task and ISR nodes;
  - topic nodes;
  - arrows for publish (node -> topic) and subscribe (topic -> node).
- It finds `ubk_advertise` / `ubk_publish*` / `ubk_subscribe` calls with a `UBK_TOPIC_*` argument and maps them to tasks with the same call graph as `ipc_inventory.py`.
- Outputs:
  - `<out>.mmd`: Mermaid.
  - `<out>.dot`: Graphviz. Render it with `dot -Tsvg <out>.dot -o <out>.svg`.
  - `<out>_n2.csv`: N2 matrix, publishers x subscribers. Use it for large systems.
- `--msg <dir>` adds queue lengths. `--focus <task or topic>` draws only the neighbourhood of a node.
- It also reports:
  - topics without a subscriber or without a publisher;
  - topics with several publishers;
  - `.msg` topics that the code never uses;
  - calls whose topic ID is not a constant.

## Verification Checklist

- [ ] Build with `UBK_CFG_CHECK_PUBLISHER_TASK=1` during integration: a publish from the wrong task returns `UBK_ERR_WRONG_TASK`.
- [ ] Check the return value of every `ubk_advertise` (once per topic) and `ubk_publish`.
- [ ] Each subscribing task has its own `ubk_sub_t` per topic, initialized with `UBK_SUB_INITIALIZER`.
- [ ] `ubk_init_all()` runs once before any publish.
- [ ] QUEUE topics: log `n_lost`. It must stay 0 in normal operation.
- [ ] Code that used to wait on a queue now waits on a notification and drains the topic.
- [ ] Each task subscribes to a topic once per cycle and works on the local copy.
- [ ] `ipc_residual.py` reports 0 references left.
- [ ] Timing: compare task execution times and interrupt latency before and after (see Performance).

## Performance

Measured with the POSIX port: 300 topics, 20 to 40 threads, WSL2 on an i9-14900HX.

| Case | Result |
|---|---|
| uncontended | the lock adds about 7 to 8 ns per call (2 to 3 times a 256 B memcpy) |
| 20 to 40 threads at 1 kHz | about 0.1 % of locks contended, 1 to 3 % of API time spent waiting |
| 20 subscribers on one topic, flat-out | throughput drops about 10x |

- Locks are per topic, so different topics never contend.
- Contention matters only when many tasks hit **the same topic** at very high rates.
- On FreeRTOS the lock is a critical section, which masks interrupts for the time of one message copy. Keep messages small (tens to a few hundred bytes) and keep large buffers out of uBroker.

### Estimate for a 500 MHz FreeRTOS target

These are **estimates, not measurements**. They assume a Cortex-M7-class core with the data cache on and messages in internal SRAM/TCM. `ubk_publish` and `ubk_subscribe` both copy inside the critical section, so the numbers apply to each call.

| Message size | Interrupts masked per call (estimate) |
|---|---|
| 64 B | about 0.3 to 0.5 µs |
| 256 B | about 0.7 to 1.5 µs |
| 1 KB | about 2.5 to 5 µs |

- **Fixed part:** about 100 to 200 cycles (0.2 to 0.4 µs) for the checks, the critical section and the sequence update.
- **Copy:** about 0.5 to 1 byte per cycle, including wait states.
- **External SDRAM or uncached memory** can make the copy several times slower. Place the topic data (`ubk_msgs.c`) in internal RAM.
- **Not a new cost.** FreeRTOS `xQueueSend` / `xQueueReceive` also copy the item inside a critical section (`prvCopyDataToQueue`). A queue converted to a QUEUE topic of the same item size masks interrupts for about the same time.
- **The real change** is for former global variables. A plain read or write had no critical section; it now has one. In return, readers no longer see half-written data.

### Are interrupts lost?

Normally no. An interrupt that arrives inside a critical section stays **pending** and runs as soon as the section ends: it is delayed, not lost.

- **Only some interrupts are masked.** FreeRTOS masks only interrupts at or below `configMAX_SYSCALL_INTERRUPT_PRIORITY`. Interrupts with a higher priority are never delayed by uBroker, but they must not call FreeRTOS or uBroker APIs (including `ubk_publish_from_isr`).
- **Lost cases.** An event can be lost only if the masked time is longer than the interrupt period:
  - **Same interrupt twice.** If one interrupt fires twice while masked, the single pending bit merges both into one call. Example: counting encoder edges with an interrupt per edge.
  - **Peripheral overrun.** A peripheral FIFO overflows before the delayed ISR reads it. Example: a fast UART or SPI with a small FIFO.
- **Rule of thumb at 500 MHz.** With messages of 256 B or less (masked about 1.5 µs or less), an interrupt is safe when its period is at least about 3 µs, i.e. below roughly 300 kHz. A 1 Mbit/s UART (10 µs per byte) is safe even without a FIFO.
- **Faster sources:**
  - Give them a priority above `configMAX_SYSCALL_INTERRUPT_PRIORITY`.
  - Or use a hardware counter or DMA.
  - Or keep the topics they compete with small.

### Reference systems

Two open-source flight controllers of similar scale, for comparison.

**PX4 (uORB, pub/sub)**
- Scale (static count of the source code):
  - 232 message definitions; there are more topics at run time because of multi-instance topics.
  - About 810 publish sites and 1,445 subscribe sites.
  - 99 modules and 42 standalone tasks.
  - Most drivers and controllers run as work items in a few work queue threads.
- Target: Pixhawk 6 class boards use an STM32H7 (Cortex-M7, 480 MHz).
- Locking: on NuttX, uORB copies the message inside a critical section, the same way as the uBroker FreeRTOS port.
- Meaning: a system of this scale, with this lock method, runs flight control on an MCU like the 500 MHz target.
- Difference: uORB wakes waiting subscribers on publish. uBroker has no such callback; use the `queue+notify` pattern.

**ArduPilot (ArduCopter)**
- Not pub/sub. Tasks get data by **calling functions** on shared objects (e.g. `AP::ahrs()`).
- The main scheduler is a cooperative single thread, so these calls need no lock. Data shared with other threads (driver backends) is copied under a semaphore, at a cost similar to uBroker.
- It is used here only as a reference for **task time budgets**.
- The main scheduler has 29 tasks at 1 to 400 Hz with maximum times of 50 to 350 µs (mostly 50 to 130 µs). That is about 1,405 task runs per second and about 11.6 % CPU if every task uses its full budget. The fast loop (attitude estimation and control) is not in this table.

**uBroker cost against these budgets** (500 MHz estimate, messages of 128 B or less, about 0.5 µs per call):

| Calls per task run | Total at 1,405 runs/s | Share of a 50 µs task |
|---|---|---|
| 5 | about 3.5 ms/s (0.35 % CPU) | about 5 % |
| 20 | about 14 ms/s (1.4 % CPU) | about 20 % |

- The whole-CPU share is small.
- The limit is the shortest tasks. Take these steps:
  - Subscribe once per cycle (see "Subscribe once per cycle").
  - Keep messages at 256 B or less.
  - Keep 1 KB messages out of 50 µs tasks; each call would take 5 to 10 % of the budget.

### Measuring on the target

- **Masked time:**
  - Toggle a GPIO at critical section entry and exit and measure the pulse with a scope.
  - Or record the worst-case DWT cycle count between `ubk_port_lock` and `ubk_port_unlock`.
- **Before and after:** compare interrupt latency and task execution times measured with the same method before and after the migration.

The tools come with sample code (`tools/migrate/sample/`) and the inventory it should produce (`expected.csv`).
