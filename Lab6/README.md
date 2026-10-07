# Lab 6: PWM with the LEDC Peripheral

Hardware PWM. Why duty resolution and frequency trade against each other as plain arithmetic, why a duty change needs a latch, and handing a whole fade to the hardware.

[![LED fading in and out on hardware PWM](screenshots/lab06_demo_led_pwm_fade_poster.jpg)](screenshots/lab06_demo_led_pwm_fade.mp4)

*Click to play. An LED on GPIO4 breathing up and down, driven entirely by the LEDC hardware fade.*

![PWM duty cycle and average voltage](screenshots/lab06_concept_pwm_duty_cycle.png)

| | |
|---|---|
| **Board** | ESP32-S3 N16R8 |
| **Peripheral** | LEDC (LED Control) |
| **Pins** | GPIO4 output |
| **Settings** | 5 kHz, 13-bit duty resolution |
| **Key APIs** | `ledc_timer_config()`, `ledc_channel_config()`, `ledc_set_duty()`, `ledc_update_duty()`, `ledc_set_fade_with_time()` |

## How it works

- **Resolution and frequency are one equation.** LEDC counts from 0 to `2^bits - 1`, so `f_pwm = f_clk / 2^bits`. At an 80 MHz clock, 13 bits tops out near 9.7 kHz, so 5 kHz fits. Asking for 13 bits at 100 kHz does not, and the driver refuses.
- **What matters depends on the load.** For an LED, more resolution matters, since coarse steps show as banding when dim. For a motor, frequency matters, so it sits above hearing range.
- **The duty change needs two calls.** `ledc_set_duty()` writes a shadow register and `ledc_update_duty()` latches it at the next period boundary. That latch exists so a change never lands mid-pulse. Forgetting the second call is the classic bug: no error, and nothing changes.
- **Hardware fade.** `ledc_set_fade_with_time()` gives the whole ramp to the peripheral. No task, no jitter, CPU free. This PWM output is what the controller in Lab 18 would drive.

## Coming from the TM4C123

The TM4C had a load register for the period and a compare register for the duty, and I worked out the load value from the system clock myself. LEDC is the same counter and compare idea. What is new is that resolution is an explicit setting, and the fade hardware has no TM4C equivalent.

## Results

| Check | Result |
|---|---|
| Frequency | Period on GPIO4 matched 5 kHz |
| Duty | High time matched 25, 50 and 75 percent commands |
| Latch | Removing `ledc_update_duty()` left the output unchanged |
| Hardware fade | Smooth ramp with the CPU idle |

## What broke

- **A duty change that did nothing.** `ledc_set_duty()` returned `ESP_OK` and the pin did not change. The value was never latched. Two minutes in the API docs, versus an hour if I had assumed the wiring was wrong.
- **Resolution rejected at a high frequency.** It reads like a driver limit, but it is just the division above not fitting.

## Build

```powershell
idf.py build flash monitor
```

LED (with a resistor) from GPIO4 to GND.
