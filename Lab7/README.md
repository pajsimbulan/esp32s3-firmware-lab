# Lab 7: The Sensor Pipeline (Phase 1 Capstone)

Assembling Labs 2 through 5 into a reusable sensor-node architecture: a hardware timer paces acquisition, a sampling task reads the IMU at a fixed rate, a window buffer accumulates samples, and a feature task computes RMS and peak on each completed window. Acquire, process, decide, communicate.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8 |
| **Peripherals** | `esp_timer`, I2C master, FreeRTOS semaphores |
| **Pins** | GPIO8/9 I2C, GPIO4 as an instrumentation probe |
| **Rates** | 1 kHz sampling, 256-sample window, ~3.9 Hz feature output |
| **Key APIs** | `xSemaphoreCreateBinary()`, `xSemaphoreGiveFromISR()`, `xSemaphoreTake()`, `xTaskCreatePinnedToCore()` |

---

## Objective

Build the architecture that every later lab plugs into. This is the first design where the answer to "why is it split this way" matters more than any individual API call, and it is the lab that turns into a whiteboard answer when someone asks how you structured your firmware.

---

## Architecture

```
esp_timer ISR (1 kHz)
      |  gives tick_sem
      v
sampler task  ---- I2C burst read of 6 bytes, scale to g, write into window[]
      |  gives win_ready every 256 samples
      v
feature task  ---- RMS and peak over the window, report
```

### Why three stages instead of one

The timer ISR does one thing: signal. It performs no I2C, no math, no logging. An I2C transaction takes hundreds of microseconds and blocks; doing that in interrupt context would stall every other interrupt on the core and violate the rule that ISRs stay short.

The sampler is the only owner of the bus. The feature task does the arithmetic, which can take variable time without ever perturbing the sample interval, because the sampler is paced by the timer rather than by the consumer.

That separation of a **fixed-rate acquisition path** from a **variable-cost processing path** is the whole point, and it generalizes to essentially every real-time data system.

### Semaphores are signals, not resources

The instinct from an OS course is to read a semaphore as guarding a resource, with take-and-release bracketing a critical section. Both semaphores here are used differently: as **one-way signals**. The ISR gives `tick_sem` and never takes it; the sampler takes it and never gives it. The count is a count of pending events, not a count of available resources. Same primitive, opposite mental model, and confusing the two produces code that deadlocks for reasons that look mysterious.

`xSemaphoreGiveFromISR()` exists as a separate function because the normal version can block, and blocking in interrupt context is not a thing that can happen. Its `pxHigherPriorityTaskWoken` output feeds `portYIELD_FROM_ISR()`, which makes the scheduler switch to the woken task immediately on ISR exit rather than at the next tick. That is the difference between microseconds and up to a full tick of latency.

### Core pinning

The sampler is pinned so that Wi-Fi and BLE work landing on the other core cannot preempt the real-time path. Being able to say why you pinned a task, rather than that you pinned it, is the part that gets asked about.

---

## Known limitation, left in deliberately

There is **one** window buffer, shared between the sampler and the feature task. Nothing prevents the sampler from beginning to overwrite the window while the feature task is still reading it. At current rates the feature computation finishes well inside one window period, so the race does not manifest, but it is a real race and not a theoretical one.

It stays documented rather than patched because the fix is the double-buffering exercise, and Lab 22 implements exactly that pattern properly with an explicit ownership invariant. A race you have identified, bounded, and explained is a better artifact than one you silently avoided.

---

## TM4C123 bridge

The ECE 425 equivalent of this was a superloop with an ISR that did everything: read the sensor, update a global, set a flag, and let the main loop notice. It works, and it collapses as soon as any stage takes variable time, because there is nothing to defer to.

The genuinely new concepts here are the ability to defer work from an ISR to a schedulable context, prioritization between stages, and the choice of which core to run on. The concept that carries over unchanged is that anything shared between an interrupt and normal code needs deliberate handling.

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| Sample rate is 1 kHz | GPIO4 toggled at the top and bottom of the sampler, captured on the analyzer |
| Windows complete at the expected rate | Feature lines at ~3.9 Hz (1 kHz / 256) in the serial log |
| Per-sample cost bounded | ~310 µs per sample, jitter under 20 µs, measured on the probe |
| Values are physically sensible | RMS near 1.0 g at rest, peak rising sharply when the board is tapped |

---

## What broke

**Feature windows arrived at 1 Hz instead of 4 Hz.** This is the best debugging story in Phase 1.

```
SAW:    Expected ~4 feature lines/sec (1 kHz / 256). Got about 1.
        RMS values looked plausible, so sampling worked. It was just slow.

TRIED:  1. Checked the timer period: 1000 us, correct.
        2. Logged a counter in the sampler: ~250 samples/sec, not 1000.
           So the timer fires but the sampler misses ticks.
        3. Toggled GPIO4 high at the top of sampler_task and low at the
           bottom, captured at 1 MHz on the analyzer.

FOUND:  The capture showed ~3.9 ms per iteration, not the ~0.3 ms I assumed.
        The I2C timeout was being hit on most reads because SCL was
        configured at 100 kHz, not 400 kHz. One config value.
        After the fix: ~310 us per sample, jitter under 20 us, windows
        at 3.9 Hz.

LESSON: I assumed the sample rate instead of measuring it. The GPIO-toggle
        trick took five minutes and would have found this immediately.
```

The generalizable technique is worth more than the fix: toggling a spare pin around a code section turns a software timing question into a waveform. It costs two GPIO writes and it is the cheapest profiler available on a microcontroller.

**I had the I2C address model wrong in my notes.**
I had written that `0x68` was a bit pattern applied to physical pins. It is a 7-bit address clocked out serially on SDA. Corrected, and it is the reason the addressing explanation in Lab 5 is as explicit as it is.

**I described polling as blocking.**
Backwards. Polling occupies the CPU checking a condition; blocking removes the task from the ready list so it consumes nothing. Getting this the wrong way round makes the entire justification for an RTOS incoherent, so it was worth being corrected on plainly.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

---

## References

- ESP-IDF Programming Guide v5.5, *FreeRTOS*, *I2C Master*, *High Resolution Timer*
- FreeRTOS API reference: `xSemaphoreGiveFromISR`, `portYIELD_FROM_ISR`
