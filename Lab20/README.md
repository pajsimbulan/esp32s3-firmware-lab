# Lab 20: Continuous ADC with DMA

Sampling a potentiometer at 20 kHz without the CPU touching a single sample. The ADC writes into a DMA pool in the background and the firmware only wakes up for full frames. Then I starve the reader on purpose to show what overflow looks like and that it gets reported, not hidden.

[![Sweeping the pot while the DMA frame stats update](screenshots/lab20_demo_pot_sweep_dma_adc_poster.jpg)](screenshots/lab20_demo_pot_sweep_dma_adc.mp4)

*Click to play. Turning the pot moves the per-frame mean from one rail to the other while frames keep arriving at a steady rate.*

| CPU polling vs DMA | Why overruns happen |
|---|---|
| ![CPU reading every sample vs DMA filling frames](screenshots/lab20_concept_cpu_vs_dma.png) | ![Reader stalls longer than the pool's slack](screenshots/lab20_concept_overrun_timeline.png) |

| | |
|---|---|
| **Board** | ESP32-S3 N16R8 |
| **Input** | Potentiometer wiper on IO1 (ADC1 channel 0) |
| **Rate** | 20 kHz, 12-bit |
| **Frames** | 1024 bytes = 256 samples = 12.8 ms, about 78 frames/s |
| **Pool** | 4 frames, about 38 ms of slack |
| **Key APIs** | `adc_continuous_new_handle()`, `adc_continuous_config()`, `adc_continuous_register_event_callbacks()`, `adc_continuous_start()`, `adc_continuous_read()` |

## How it works

- **The CPU sets it up and walks away.** The ADC and DMA fill the pool on their own. A conversion-done callback fires per frame and the reader task drains it.
- **Slack is a budget.** The pool holds 4 frames. If the reader is late by more than about 38 ms, new samples have nowhere to go.
- **Overflow is reported.** The pool-overflow callback is counted, and when a read fails because the pool overflowed the firmware logs `pool overflow -- samples lost`. Setting `STALL_MS 200` makes the reader sleep longer than the slack and proves the warning fires. The code currently ships with `STALL_MS 200`, so set it to 0 for the normal case.
- **Rate is measured, not assumed.** The log prints frames per second and the mean value, so a wrong clock divider shows up right away.

## Results

| Case | Result |
|---|---|
| Normal reader (`STALL_MS 0`) | About 78 frames/s, no overflow, mean follows the pot |
| `STALL_MS 200` | Overflow warnings every stall, samples lost and counted |

## Build

```powershell
idf.py build flash monitor
```
