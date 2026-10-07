# Lab 19: Labeled Data Capture for On-Device ML

The data side of an embedded ML project. Before any model can run on a microcontroller, someone has to collect clean, labeled feature windows from the real sensor at a fixed rate. This firmware does that: IMU magnitude sampled at 1 kHz, cut into 256-sample windows, reduced to RMS and peak, and printed as labeled CSV ready for training.

| | |
|---|---|
| **Board** | ESP32-S3 N16R8 |
| **Sensor** | MPU-6500 over I2C at 400 kHz (SDA IO8, SCL IO9, address `0x68`) |
| **Sampling** | 1 kHz with `vTaskDelayUntil`, 256-sample windows, about 3.9 windows/s |
| **Features** | RMS and peak of the mean-removed magnitude |
| **Labels** | Hold BOOT (IO0) to tag windows as `fault`, release for `normal` |
| **Output** | CSV `session,label,rms,peak` over serial |

## How it works

- **Features, not raw samples.** A 256-sample window becomes two numbers. Small inputs keep any later model small enough for an MCU.
- **Fixed cost per window.** Feature extraction is two passes over the window no matter what the data looks like, so the timing never depends on the input.
- **The tick rate is checked at compile time.** `_Static_assert(configTICK_RATE_HZ == 1000)` stops the build if the tick is 100 Hz, where `pdMS_TO_TICKS(1)` would round to zero and the loop would spin.
- **The rate is checked at run time.** Every 40 windows it logs the measured window rate against the expected 3.90/s, so a slow I2C bus or a dropped sample shows up immediately.
- **Sessions.** `SESSION_ID` is a build flag, so captures from different runs can be merged into one dataset and still be told apart.
- **Labeling with one button.** No host tool needed. The label is read from BOOT at the end of each window, so the person shaking the board decides the label in real time.

## Scope

This lab produces the dataset. Training a model and deploying it on the device is the next step and is not in this repo.

## Build

```powershell
idf.py build flash
idf.py monitor | Tee-Object session0.csv
```

Shake or tap the board while holding BOOT for `fault` windows, leave it still or gently moving for `normal`.
