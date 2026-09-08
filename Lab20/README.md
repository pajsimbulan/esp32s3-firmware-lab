# Lab 20: Continuous ADC with DMA

Sampling an analog signal at rates the CPU cannot service per-sample, by letting the DMA engine move conversion results into memory frames while the CPU sleeps. Frame sizing, buffer pool management, overrun detection, and distinguishing a lost sample from an idle bus.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8 |
| **Peripherals** | ADC1 in continuous (DMA) mode |
| **Pins** | GPIO1, ADC1 channel 0. Potentiometer, or a jumper to 3V3 / GND |
| **Rate** | 20 kHz, 1024-byte frames |
| **Key APIs** | `adc_continuous_new_handle()`, `adc_continuous_config()`, `adc_continuous_register_event_callbacks()`, `adc_continuous_read()` |

---

## Objective

The lab's own framing is the reason it is here: *"I have used DMA" and "I have configured DMA" are different claims, and interviewers can tell them apart in one question.* This configures it, sizes the frames from the arithmetic, and handles the failure modes.

---

## How it works

### DMA is parallelism, not concurrency

This distinction is worth being precise about. Concurrency is one execution unit interleaving between tasks. DMA is a **separate bus master** physically moving data at the same time as the CPU executes, contending for bus cycles rather than for CPU time. The samples land in memory whether or not the CPU is doing anything, which is why the CPU can block on a semaphore or sleep between frames.

DMA also has **no memory of its own**. The buffer pool comes out of internal SRAM, which is why it must be DMA-capable memory and why the pool size is a real constraint rather than a free parameter.

### Frame arithmetic, and the mistake that lives here

One conversion result on the ESP32-S3 is **4 bytes**, not 2. It is a struct containing the raw value plus channel and unit metadata, and its size is `SOC_ADC_DIGI_RESULT_BYTES`.

So a 1024-byte frame holds **256 samples**, not 512.

At 20 kHz that gives a frame every 12.8 ms, or **78.1 frames per second**. An earlier version of this lab assumed 2 bytes per result and reported roughly half that rate. The number was wrong by exactly 2x, which is the most dangerous kind of wrong because it is plausible.

The general rule this teaches: never assume the width of a hardware result struct. Look up the SoC capability macro. It differs between ESP32 variants, which is exactly the sort of thing that breaks a port.

Frame size is a latency-versus-overhead tradeoff. Small frames mean more interrupts and lower latency; large frames mean fewer wakeups and more buffering delay.

### Overrun is detectable, and I initially thought it was not

If the CPU does not drain frames fast enough, the pool fills and samples are lost. Two mechanisms report this:

- **`on_pool_ovf`**, an event callback that fires on overflow.
- **`adc_continuous_read()` returning `ESP_ERR_INVALID_STATE`** when samples have been dropped.

An earlier draft of this lab asserted that pool overrun was silent. It is not, and the correction matters because silent data loss and reported data loss are completely different engineering situations. A system that reports its own overruns can degrade honestly; one that does not will hand you plausible garbage.

The related discipline is **discriminating the error cases**. `ESP_ERR_TIMEOUT` (no data available yet) and `ESP_ERR_INVALID_STATE` (data was lost) are entirely different conditions, and treating both as "nothing to read" turns dropped samples into silence.

### ADC1, never ADC2

**ADC2 shares hardware with the Wi-Fi radio.** With Wi-Fi active, ADC2 reads fail or return garbage, and this is documented rather than a bug. Any design that needs both connectivity and analog input uses ADC1. Discovering this after wiring is a bad afternoon.

Also worth knowing: the ADC is not linear across its full range and Espressif ships per-chip calibration data in eFuse, reached through the calibration API. Raw counts are not volts.

---

## TM4C123 bridge

ECE 425 moved every ADC sample with the CPU: start a conversion, poll the completion flag or take an interrupt, read `ADCSSFIFO`, repeat. That works to a few kHz and then the CPU is doing nothing else.

This is the answer to sampling at 20 kHz without spending the whole core on it. The TM4C has a µDMA controller that could do the same; the coursework never used it. The conceptual jump is that the CPU stops touching individual samples entirely and starts handling **frames**, which changes the shape of the application code as much as the performance.

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| Conversions running | Frames arrive continuously with the CPU blocked between them |
| Frame arithmetic correct | 1024-byte frame yields 256 results at `SOC_ADC_DIGI_RESULT_BYTES` = 4 |
| Rate as configured | Frame period 12.8 ms at 20 kHz, 78.1 frames/sec, confirmed against `esp_timer` timestamps |
| Data is real | Rotating the potentiometer moves the values across the full range monotonically |
| Overrun detected | Deliberately stalling the consumer triggers `on_pool_ovf` and `ESP_ERR_INVALID_STATE` rather than silent loss |
| Error cases distinguished | Timeout and invalid-state handled separately in the read path |

---

## What broke

**Sample-rate arithmetic wrong by 2x.**
Covered above: 4 bytes per conversion result, not 2. Found by dividing the observed frame rate into the configured sample rate and getting a number that did not match the assumption, then checking `SOC_ADC_DIGI_RESULT_BYTES` rather than trusting the assumption.

**An earlier version of this lab did not compile.**
Struct field names and the callback signature had drifted from the v5.5 API. Worth recording only because it is the standard fate of any code written against a version you are not building on, and the fix is reading the header in your own installation rather than the documentation you found.

**Overrun assumed silent.**
An incorrect claim in my own material, corrected against the ESP-IDF source. Documented in the repository errata rather than quietly edited, because how a defect was found is more interesting than that it existed.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

Potentiometer wiper to GPIO1, ends to 3V3 and GND. A jumper straight to 3V3 or GND works for a fixed-level check.

---

## References

- ESP-IDF Programming Guide v5.5, *Analog to Digital Converter (ADC) Continuous Mode Driver*, *ADC Calibration*
- ESP32-S3 Technical Reference Manual, ADC and GDMA chapters
- `soc/soc_caps.h`, `SOC_ADC_DIGI_RESULT_BYTES`
