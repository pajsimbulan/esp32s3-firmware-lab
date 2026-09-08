# Lab 4: Hardware Timers and Fixed-Rate Sampling

Generating a 1 kHz periodic event in hardware instead of in a delay loop, handing it safely to a task across an interrupt boundary, and then verifying the period externally with a logic analyzer rather than trusting the firmware's own report of itself.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8 |
| **Peripherals** | `esp_timer` (high-resolution timer), GPIO |
| **Pins** | GPIO4 as an instrumentation probe |
| **Key APIs** | `esp_timer_create()`, `esp_timer_start_periodic()`, `esp_timer_get_time()`, `gpio_set_level()` |
| **Instrumentation** | FX2LP analyzer, CH0 on GPIO4, 1 MHz sample rate |

---

## Objective

Produce the sample clock that every sensor node needs. A 1 kHz callback drives a counter and toggles a probe pin; the serial log reports callbacks per second and the analyzer independently confirms the period on the wire.

The point of the external measurement is that a firmware counter reporting 1000 per second proves the counter increments 1000 times, not that the interval between them is uniform. Jitter is invisible from inside.

---

## How it works

### The timer is free-running

`esp_timer` is backed by a 64-bit hardware counter running continuously since boot, not a counter that resets when you start a timer. `esp_timer_start_periodic()` registers a callback against an absolute deadline computed from that counter, and reschedules the next deadline from the previous target rather than from the callback's completion. The period does not accumulate the callback's own execution time, which is the same absolute-target principle as `vTaskDelayUntil()`, just in hardware and with microsecond rather than tick resolution.

The period argument is in **microseconds**. 1 kHz is `1000`, which reads deceptively like milliseconds.

### Why a hardware timer instead of the scheduler

At the default 100 Hz FreeRTOS tick, one tick is 10 ms and 1 kHz is not expressible with `vTaskDelay` at all. Even at a 1000 Hz tick, the scheduler quantizes to whole ticks and the delay is subject to whatever else is running. A hardware timer's deadline is enforced by the counter, so the only variability left is interrupt latency.

### `volatile` and what it does not do

The callback runs in interrupt context and the reporting task reads the same counter, so the counter is declared `volatile`. That tells the compiler it may change outside the visible control flow, so it must reload from memory rather than cache the value in a register across a loop. Without it, a poll loop can be optimized into an infinite loop reading a stale register.

What `volatile` does **not** provide is atomicity. `samples++` is a read-modify-write: three operations, interruptible between any two. `volatile` guarantees the read is fresh and the write happens; it guarantees nothing about the sequence being indivisible. For a counter written only by the ISR and read only by the task this is acceptable, but the distinction is the whole answer to a very common interview question, and the general solution is an atomic type, a critical section, or a queue.

### The signal on the wire is half the callback rate

Toggling GPIO4 on each callback means one full high-low cycle spans two callbacks. A 1 kHz timer produces a 2 ms period, so **500 Hz fundamental**. Confusing the event rate with the signal rate is an easy way to convince yourself the timer is running at half speed.

---

## Measurement notes

Sample rate on the analyzer was set to 1 MHz, giving one sample per microsecond and locating any edge to ±1 µs. Against 1 ms intervals that is a 0.1 percent measurement floor, so a few microseconds of observed jitter is real timer jitter rather than instrument quantization.

Worth being precise about why Nyquist is not the criterion here. A square wave is not bandlimited: its Fourier series has odd harmonics decaying as 1/n out to infinity, so the sampling theorem's precondition is violated and no sample rate satisfies it. But a logic analyzer is a 1-bit quantizer thresholding against a logic level and timestamping transitions, not a system reconstructing an analog waveform from samples. The criterion that applies is edge timing resolution, not twice the bandwidth. Practical rule carried forward: 10x the fundamental to confirm a signal is roughly right, 100x or more to measure jitter.

Undersampling a digital signal also fails differently from undersampling an analog one. Instead of a clean aliased frequency, you get randomly missed pulses, because the quantizer is tracking a signal changing faster than it can sample.

---

## TM4C123 bridge

The TM4C equivalent was a general-purpose timer configured by hand: prescaler in `GPTMTAPR`, interval in `GPTMTAILR`, mode bits in `GPTMTAMR`, interrupt unmasked in `GPTMIMR`, and the flag cleared in `GPTMICR` at the top of every ISR or the interrupt re-fires immediately. Forgetting that `ICR` write is a classic.

`esp_timer` hides the prescaler and the flag clearing but the mechanism is the same: a hardware counter, a compare value, an interrupt. What is genuinely new is the software timer layer on top, which multiplexes many logical timers onto one hardware counter and runs their callbacks from a dedicated high-priority task rather than from raw interrupt context, unless the timer is created as ISR-dispatched.

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| Callback rate is exactly 1 kHz | Serial log reports 1000 callbacks per second, sustained |
| Period is uniform on the wire | PulseView capture of GPIO4 shows a 2 ms period square wave, measured 501.5 Hz by cursor (placement precision, not clock error) |
| Jitter is within the measurement floor | Edge spacing stable to within the ±1 µs resolution at 1 MHz |

---

## What broke

**A checksum mismatch that was not a corrupted flash.**
The chip was running the Lab 3 image while I was building and monitoring from the Lab 4 directory. The mismatch was real; the diagnosis of "bad flash" was not. This is the failure mode that pushed me off the VS Code flash button and onto explicit `idf.py -p COM3 build flash monitor`, where the target directory is unambiguous.

**`esptool` could not open COM3.**
A monitor session from the previous run still held the port. Windows gives no useful indication of which process owns a COM port. Close the monitor before flashing, or accept a confusing permission error.

**The logic analyzer enumerated as an unknown device.**
The FX2LP presents no usable Windows driver out of the box. It appears under "Other devices" in Device Manager (confirm by unplug and replug), and Zadig binds WinUSB to it at USB ID `0925:3881`, after which `fx2lafw` in PulseView finds it. Also note the FX2LP only supports fixed sample rates that are integer divisions of 48 MHz: 1, 2, 3, 4, 6, 8, 12, 16, 24 MHz. Asking for anything else silently gets you the nearest supported rate.

**Flat traces on every channel.**
Almost always a missing common ground between the analyzer and the board, before it is anything else.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

Analyzer: CH0 to GPIO4, GND to GND, 1 MHz, 1 M samples.

---

## References

- ESP-IDF Programming Guide v5.5, *High Resolution Timer (esp_timer)* and *General Purpose Timer (GPTimer)*
- ESP32-S3 Technical Reference Manual, Chapter 12 (System Timer), Chapter 13 (Timer Group)
- sigrok / PulseView documentation for `fx2lafw`
