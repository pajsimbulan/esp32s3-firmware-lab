# Lab 18: PID Control with Anti-Windup

Closed-loop control on a microcontroller: proportional, integral, and derivative terms implemented in fixed-interval discrete time, the integral windup problem made visible, and two of the standard fixes applied and measured.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8 |
| **Structure** | Fixed-rate control task, plant model, step-response logging |
| **Techniques** | Conditional integration anti-windup, derivative-on-measurement, output saturation |

---

## Objective

PID comes up in interviews for anything involving motors, thermal management, or power. The part that separates people who have implemented one from people who have read the equation is **windup**, so this lab builds the naive version first, breaks it deliberately, and then fixes it.

---

## How it works

### Discrete time, fixed interval

The textbook form is continuous. On a microcontroller it becomes:

```
error      = setpoint - measurement
integral  += error * dt
derivative = (error - error_prev) / dt
output     = Kp*error + Ki*integral + Kd*derivative
```

`dt` must be **constant**, which is why the controller runs in its own task paced by a hardware timer, exactly like the sampler in Lab 7. A control loop paced by `vTaskDelay` in a loop that also does variable-cost work has a `dt` that drifts, so `Ki` and `Kd` are effectively being multiplied by a random number each iteration. Correct fixed-rate execution is a prerequisite for the maths meaning anything.

### Windup

Every real actuator saturates: a motor has a maximum duty, a heater a maximum power. When the output is clamped at its limit, the error stays large because the plant cannot respond any faster, and the integral term **keeps accumulating** the whole time.

Once the process reaches the setpoint, that accumulated integral has to be unwound before the output can come down. The result is a large overshoot and a long settling time that has nothing to do with the gains being wrong. The controller is fighting a number it stored while it was helpless.

Two fixes, both implemented here:

- **Conditional integration.** Stop accumulating the integral while the output is saturated. Simple, and effective enough that it is the default choice.
- **Back-calculation.** Feed the difference between the commanded and saturated output back into the integral. Smoother, one more tuning parameter.

### Derivative on measurement

The derivative of the *error* includes the derivative of the *setpoint*. A step change in setpoint is an infinite derivative, so the controller emits a huge output spike the instant an operator changes a target. This is called derivative kick.

Differentiating the **measurement** instead of the error removes it, because the measurement is continuous. The sign flips, and that is the entire change. It is one of those fixes that is trivial to apply and impossible to guess if you have not seen it.

The derivative term also amplifies sensor noise by construction, which is why it is often left at zero in practice, and why a low-pass filter on the derivative is standard when it is not.

---

## Experiment

Three configurations, same plant, same step input:

| Configuration | Expected behaviour |
|---|---|
| P only | Reaches steady state with a persistent offset, because a nonzero error is required to produce any output |
| PI, no anti-windup | Offset eliminated, but large overshoot and long settling caused by windup during saturation |
| PI with conditional anti-windup | Offset eliminated, overshoot substantially reduced |

The step responses are logged over serial for each configuration and compared directly. The P-only offset is the clean demonstration of why the integral term exists at all, and the difference between the second and third rows is the entire point of the lab.

---

## TM4C123 bridge

No control theory in ECE 425. The relevant carry-over is mechanical rather than conceptual: fixed-rate execution from a hardware timer, integer versus float arithmetic under embedded constraints, and PWM as the actuator output, which is Lab 6.

Worth naming that PID here is implemented against a software plant model rather than a physical motor. That is stated plainly rather than dressed up, because a controller tuned against a simulated first-order plant is a demonstration of the algorithm and its failure modes, not evidence of tuning a real machine. Those are different claims and it is better to make the accurate one.

---

## What broke

**The anti-windup condition was inverted, and the result was worse than no anti-windup at all.**

The conditional-integration guard was written so that the integral accumulated **only** while saturated, which is precisely backwards. Instead of preventing windup, it did nothing but wind up.

The reason this is the best story in the lab is that the bug did not present as a failure. The controller ran, converged, and produced a plausible-looking step response. It just overshot more than the version with no protection at all, which is a result that is easy to rationalize as needing retuning. Catching it required comparing all three configurations against each other and noticing that the ordering was wrong: the "protected" case cannot legitimately be worse than the unprotected one.

The transferable point is that control code fails quietly. There is no exception and no error code, just a number that is worse than it should be, and the only way to catch it is to have an expectation about the relationship between configurations before running them.

**Derivative term unusable on real sensor data.**
Differentiating a noisy signal amplifies the noise, and the derivative output was larger than the signal. Expected from the maths and startling in practice.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

Each configuration prints its step response as a time series for comparison.

---

## References

- Åström and Hägglund, *PID Controllers: Theory, Design, and Tuning*, on windup and back-calculation
- ESP-IDF Programming Guide v5.5, *High Resolution Timer*, *LED Control (LEDC)*
