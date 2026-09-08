# Lab 6: PWM with the LEDC Peripheral

Generating a pulse-width-modulated output in hardware, understanding why duty resolution and frequency trade against each other as arithmetic rather than preference, and why a duty update needs a latch.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8 |
| **Peripherals** | LEDC (LED Control) |
| **Pins** | GPIO4 output, probed on the analyzer |
| **Configuration** | 5 kHz, 13-bit duty resolution |
| **Key APIs** | `ledc_timer_config()`, `ledc_channel_config()`, `ledc_set_duty()`, `ledc_update_duty()`, `ledc_set_fade_with_time()` |

---

## Objective

Fade an LED smoothly, then hand the fade off to hardware entirely. Short lab, but it closes out peripheral coverage for Phase 1 and supplies the actuator output that the control work in Lab 18 needs.

---

## How it works

### Duty resolution is a division problem

LEDC is a counter driven by a source clock. It counts from zero to a maximum of `2^resolution_bits - 1`, and the output is high while the count is below the duty value. The output frequency is therefore the source clock divided by that count range:

```
f_pwm = f_clk / 2^resolution_bits
```

Which means resolution and frequency are not independent choices. At an 80 MHz APB clock, 13 bits gives a ceiling around 9.7 kHz, so 5 kHz at 13 bits fits comfortably. Asking for 13-bit resolution at 100 kHz does not fit and the driver refuses it. This is the tradeoff people describe as a design preference when it is actually one equation, and being able to state it that way is a clean interview answer.

For LED brightness, higher resolution matters more than higher frequency, because human perception of brightness is nonlinear and coarse duty steps are visible as banding at the dim end. For motor drive, frequency matters more, because it needs to sit above the audible range.

### The duty update needs two calls

`ledc_set_duty()` writes the new value into a shadow register. `ledc_update_duty()` latches it into the active register, and the hardware applies it at a period boundary. That is a **double-buffered latch**, and it exists so a duty change cannot land mid-period and emit a malformed pulse.

Forgetting the second call is the classic LEDC bug: the code looks right, the value is written, and nothing on the output changes. The same shadow-register-plus-commit pattern shows up in the framebuffer model in Lab 1 and again in the DMA buffer handoff in Lab 22.

### Hardware fade

`ledc_set_fade_with_time()` hands the entire ramp to the peripheral: the hardware steps the duty over the requested interval with no CPU involvement and raises an interrupt at completion. The CPU-driven version (a loop calling `set_duty` and `update_duty` with a delay) is functionally similar and costs a task, jitters with scheduler load, and is the wrong answer when a timing-sensitive job is running on the same core.

---

## TM4C123 bridge

The TM4C had two routes. The PWM module proper used a load register for the period and a compare register for the duty, with the pin driven by the comparator output. Alternatively a general-purpose timer in PWM mode did the same with `GPTMTAILR` and `GPTMTAMATCHR`. Either way you computed the load value from the system clock yourself.

LEDC is the same counter-and-compare structure. The differences worth naming are that the resolution is an explicit configuration parameter rather than an implicit consequence of the load value's width, and that the fade hardware has no TM4C equivalent at all.

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| Frequency as configured | PulseView measured the period on GPIO4 against the 5 kHz target |
| Duty is what was requested | High-time to period ratio matched the commanded duty at 25 / 50 / 75 percent |
| Latch behavior confirmed | Removing `ledc_update_duty()` left the output unchanged despite `set_duty` succeeding |
| Hardware fade runs unattended | Brightness ramps smoothly with the CPU idle rather than spinning in a loop |

---

## What broke

**A duty change that had no effect.**
`ledc_set_duty()` returned `ESP_OK` and the output did not move. The shadow register was written and never latched. Diagnosing this by reading the API documentation took two minutes; assuming the pin was wrong would have taken an hour.

**Resolution rejected at the requested frequency.**
The driver returns an error rather than silently degrading, which is the right behavior and initially reads as a driver limitation. It is arithmetic: the requested `2^bits` count range did not divide into the source clock at that frequency.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

Analyzer: CH0 to GPIO4, 4 MHz, 1 M samples. Measure duty directly; no decoder needed.

---

## References

- ESP-IDF Programming Guide v5.5, *LED Control (LEDC)*
- ESP32-S3 Technical Reference Manual, Chapter 30 (LED PWM Controller)
