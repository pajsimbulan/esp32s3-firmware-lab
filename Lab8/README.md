# Lab 8: GPIO Interrupts and ISR-Safe Code

Edge-triggered interrupts on a real button, an ISR that does nothing but post an event, and a worker task that does the actual work. The universal "keep ISRs tiny" pattern, plus the memory-placement rules that make an ISR safe to run at all.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8 |
| **Peripherals** | GPIO interrupt, FreeRTOS queue |
| **Pins** | GPIO0 BOOT button, falling-edge triggered |
| **Key APIs** | `gpio_install_isr_service()`, `gpio_isr_handler_add()`, `xQueueCreate()`, `xQueueSendFromISR()`, `xQueueReceive()`, `portYIELD_FROM_ISR()`, `IRAM_ATTR` |
| **Instrumentation** | FX2LP analyzer on GPIO0 at 12 MHz to resolve contact bounce |

---

## Objective

Replace Lab 1's polling loop with a hardware interrupt, and structure the handler the way production firmware does: the ISR captures which pin fired and posts it to a queue, and a task blocked on that queue performs the debounce and the response.

---

## How it works

### The two-part handler

The ISR runs with interrupts disabled on that core, so every microsecond it spends is latency added to every other interrupt in the system. It therefore does the minimum: read the pin number, push it into a queue, request a context switch, return. Everything with variable cost (debouncing, logging, driving the LED) happens in a normal task.

This split has names in other ecosystems (top half and bottom half in Linux, deferred procedure call in Windows) and it comes up by name in interviews. The ESP-IDF version is the ISR plus a queue plus a worker task.

### `xQueueSendFromISR` and the yield

`xQueueSend` can block if the queue is full, and blocking in interrupt context is impossible: there is no task to suspend. The `FromISR` variant never blocks and instead returns failure, and it outputs `pxHigherPriorityTaskWoken` through a pointer.

That flag matters. If posting to the queue unblocked a task of higher priority than whatever was interrupted, `portYIELD_FROM_ISR()` makes the scheduler switch to it the moment the ISR returns, rather than waiting for the next tick. At a 100 Hz tick that is the difference between microseconds of latency and up to 10 milliseconds.

### `IRAM_ATTR` and why it is not decoration

Application code lives in flash and is reached through a cache. A cache miss stalls while the fetch completes, which is fine in a task and unacceptable in an ISR, and outright fatal if the interrupt fires while the flash controller is busy (during an SPI flash write, for instance) because the code simply cannot be fetched.

`IRAM_ATTR` places the function in internal RAM, which is always accessible with deterministic timing. The subtlety that catches people is that it covers the **function**, not what the function touches: string literals still live in `.rodata` in flash, and any non-IRAM function called from the ISR is still a flash fetch. `ESP_LOGI` inside an ISR is the standard version of this mistake.

### The queue handle is opaque

`xQueueCreate(8, sizeof(uint32_t))` heap-allocates a control block holding the storage buffer, head and tail indices, item count, item size, and the lists of tasks blocked on it, and returns an opaque pointer. You cannot dereference it, deliberately, so FreeRTOS can change its internals without breaking application code. Same pattern as `i2c_master_dev_handle_t` in Lab 5 and `SemaphoreHandle_t` in Lab 7: one concept, three appearances.

---

## TM4C123 bridge

On the TM4C this was raw NVIC work: enable the interrupt in `GPIOIM`, select the edge in `GPIOIS` / `GPIOIBE` / `GPIOIEV`, set priority in the NVIC priority registers, enable it in `NVIC_EN0`, and then in the handler **clear the flag in `GPIOICR`** or the interrupt re-fires forever. The vector table entry was placed by hand in the startup file.

The hardware layer here is identical in spirit; `gpio_install_isr_service()` installs one shared handler and `gpio_isr_handler_add()` registers per-pin callbacks that it dispatches to, and the flag clearing happens inside that shared handler.

What is genuinely new is that there is somewhere to defer work **to**. On the TM4C, deferring meant setting a global flag and hoping the superloop got around to it. Here the ISR can wake a specific task at a specific priority and the scheduler enforces it.

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| Interrupt fires on press | Worker task logs one event per physical press |
| ISR stays minimal | No logging, no delays, no blocking calls inside the handler |
| Edge behavior confirmed physically | PulseView capture of GPIO0 at 1 µs resolution; the **release edge is clean** with no measurable bounce |
| Queue decouples correctly | Worker's 20 ms debounce delay does not affect interrupt latency |

Press-edge bounce measurement is still outstanding. Noted honestly rather than claimed.

---

## What broke

**The interrupt was enabled before the handler was registered.**
`gpio_config()` with `.intr_type` set arms the interrupt at configuration time, and `gpio_isr_handler_add()` came after it in my code. Any edge arriving in that window dispatches into a handler that does not exist yet. It never bit me in practice because a human cannot press a button that fast, which is exactly what makes it a dangerous ordering bug: correct-looking, untested by the physical world, and fatal the moment the interrupt source is something faster than a finger. Configure the pin, register the handler, then enable.

**The debounce filter failed on held presses.**
The worker's fixed `vTaskDelay` after an event assumes the button is released within that window. Holding it produces repeat events. Correct for the lab, wrong as a general debouncer, and documented rather than hidden.

**A log timestamp pattern I nearly ignored.**
Events were spaced exactly 20 ms apart, which was suspiciously exactly my own debounce delay value rather than any property of the hardware. Following that observation to a measurement rather than stopping at the hypothesis is the habit this whole track is training.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

Analyzer: CH0 to GPIO0, GND to GND, 12 MHz with a falling-edge trigger to catch bounce.

---

## References

- ESP-IDF Programming Guide v5.5, *GPIO* (interrupt section), *Interrupt Allocation*, *Memory Types* (IRAM/DRAM/flash cache)
- FreeRTOS API reference: `xQueueSendFromISR`, `portYIELD_FROM_ISR`
