# esp32s3-firmware-lab

A self-directed embedded firmware track on the Espressif ESP32-S3: peripheral drivers written against the technical reference manual, FreeRTOS concurrency, DMA-backed data movement, wireless connectivity, OTA with rollback, and JTAG-level debugging. Twenty-three standalone ESP-IDF projects, built and verified on hardware.

![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.5-red)
![Target](https://img.shields.io/badge/target-ESP32--S3-blue)
![Language](https://img.shields.io/badge/C-75.9%25-brightgreen)
![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS-orange)
![License](https://img.shields.io/badge/license-MIT-lightgrey)

---

## What this is

Each folder in this repository is a complete, independently buildable ESP-IDF project with its own `main.c`, `CMakeLists.txt`, `sdkconfig.defaults`, and README. The track starts at a blank toolchain and ends at a device that samples an IMU over I2C at a fixed rate, processes it under a preemptive RTOS, moves bulk data with DMA, reports over Wi-Fi and BLE, updates itself over the air with a rollback safety net, and can be halted and inspected over JTAG.

The firmware is hand-written. Where an ESP-IDF driver is used, the register-level behavior underneath it is explained in that lab's README, because the point of the exercise is to understand the peripheral, not the wrapper.

**What this is not:** an Arduino sketch collection, or vendor examples with the comments reworded.

## Why it exists

My baseline is bare-metal TI Tiva C (TM4C123, Cortex-M4F, direct register access, no HAL) from graduate coursework. Re-implementing the same peripheral set (GPIO, UART, timers, I2C, SPI, PWM, ADC) on a second silicon vendor with a completely different driver philosophy is the fastest way to separate *the peripheral* from *one vendor's implementation of it*. Every lab README below opens with a **TM4C123 bridge** note doing exactly that comparison.

The secondary goal was to get past the edge of that coursework: interrupt discipline, priority inversion, CAN, DMA descriptors, OTA state machines, on-target unit testing, and post-mortem debugging are things production firmware teams live in and student projects usually skip.

---

## Bench

| | |
|---|---|
| **MCU** | Lonely Binary ESP32-S3 N16R8: dual-core Xtensa LX7 @ 240 MHz, 16 MB flash, 8 MB octal PSRAM |
| **Framework** | ESP-IDF v5.5.5, CMake, `xtensa-esp32s3-elf` GCC, `idf.py` CLI |
| **Sensor** | TDK InvenSense MPU-6500 6-axis IMU over I2C (`WHO_AM_I = 0x70`, bus address `0x68`) |
| **Bus hardware** | 2x SN65HVD230 CAN transceivers, 120 Ω termination, 74HC595 shift register, ST7789 240x320 SPI TFT |
| **Instrumentation** | Cypress FX2LP 8-channel logic analyzer @ 24 MS/s, `fx2lafw` under PulseView |
| **Debug** | On-chip USB-Serial-JTAG (GPIO19/20), OpenOCD + GDB, ESP-IDF core dumps |
| **Host** | Windows 11, VS Code with clangd against `build/compile_commands.json`, Python 3.14 for host-side receivers |

---

## Layout

```
Lab0/    ... Lab22/       one folder per lab, each a complete idf.py project
  main/main.c             the firmware for that lab
  CMakeLists.txt
  sdkconfig.defaults      committed. sdkconfig is gitignored, see "Gotchas"
  README.md               objective, walkthrough, verification, what broke
```

Build any lab:

```powershell
cd Lab5
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

Each lab README states the expected serial output and the acceptance criteria, so a successful run is distinguishable from a plausible-looking one.

---

## Lab index

### Phase 1: Foundations

| # | Lab | What it demonstrates |
|---|---|---|
| 00 | Toolchain and first flash | ESP-IDF project model, CMake component system, build/flash/monitor loop |
| 01 | GPIO: RGB LED and BOOT button | `gpio_config_t`, 64-bit pin bitmasks, active-low input, polling debounce, RMT-driven WS2812 |
| 02 | FreeRTOS tasks and drift-free timing | Preemptive scheduling, task priorities, `vTaskDelayUntil` vs `vTaskDelay`, why superloops accumulate drift |
| 03 | UART and loopback | Framing, baud rate error, driver ring buffers, a serial command parser with stateful line accumulation |
| 04 | Hardware timers | `esp_timer` vs GPTimer, `volatile` across ISR/task boundaries, jitter measured on the analyzer |
| 05 | I2C and IMU bring-up | Address probe, `WHO_AM_I`, register map from datasheet, burst reads, two's-complement reassembly to g |
| 06 | PWM (LEDC) | Duty resolution vs frequency tradeoff, hardware fade, duty verified on the analyzer |
| 07 | Sensor pipeline | Producer/consumer with counting semaphores, window buffering, RMS/peak feature extraction, core pinning |

### Phase 2: Past the coursework

| # | Lab | What it demonstrates |
|---|---|---|
| 08 | GPIO interrupts | ISR context rules, `IRAM_ATTR`, deferring work to a task through a queue, ISR-safe API discipline |
| 09 | SPI master | Full-duplex framing, CS timing, mode 0 (CPOL/CPHA), MOSI-to-MISO loopback, 74HC595 as a real slave |
| 10 | NVS | Wear-levelled key/value storage, namespace lifecycle, partition tables |
| 11 | On-target unit testing | Unity test runner executing on the target, testing logic with no hardware underneath it |
| 12 | Deep sleep | Wake sources, RTC memory retention, power budgeting for a battery-backed node |
| 13 | Wi-Fi and telemetry | Station bring-up, event-driven connection handling, JSON POST, retry and backoff |
| 14 | BLE GATT | NimBLE service and characteristic model, notifications, verified in nRF Connect |
| 15 | OTA with rollback | Dual-app partitions, probation state machine, self-test, automatic rollback on failure |
| 16 | Priority inversion | Inversion reproduced deterministically, then fixed with priority inheritance and measured both ways |
| 17 | CAN / TWAI | Message-ID arbitration, hardware acceptance filters, error counters, bus-off detection and recovery |
| 18 | PID control | Anti-windup, derivative-on-measurement, step responses recorded across three controller configurations |

### Phase 3: Gap-closers

| # | Lab | What it demonstrates |
|---|---|---|
| 19 | Inference harness | Fixed-cost feature extraction, quantized model on an MCU, tensor arena sizing, deterministic latency budget |
| 20 | Continuous ADC with DMA | DMA frame sizing, conversion-result packing, pool overflow detection, overrun recovery |
| 21 | JTAG, OpenOCD, GDB | Breakpoints, hardware watchpoints, RTOS-aware backtraces, catching a memory corruption bug live |
| 22 | SPI TFT with DMA | Ping-pong double buffering, explicit buffer-ownership invariant released from an ISR callback, throughput measured in fps |

---

## Selected debugging

Project lists are cheap. What separates someone who has built firmware from someone who has followed a tutorial is whether they can describe a bug they did not understand at the start. Each lab README carries a **What broke** section; these are the ones worth reading first.

| Lab | Symptom | Root cause |
|---|---|---|
| 00 | Build silently targets the wrong chip after copying a project | `IDF_TARGET` is cached in `sdkconfig`, and `sdkconfig.defaults` is only read when no `sdkconfig` exists |
| 05 | `WHO_AM_I` returns `0x70` where every tutorial says `0x68` | The breakout carries an MPU-6500, not an MPU-6050. Bus address and register map are identical, the device ID is not. Reading `0x70` means the wiring is correct |
| 07 | Feature windows arriving at 1 Hz instead of the expected 4 Hz | I toggled a GPIO at the top and bottom of the sampler task and captured it: each iteration took 3.9 ms, not the 0.3 ms I assumed. I2C was configured at 100 kHz, not 400 kHz, so most reads were hitting the timeout. One config value |
| 13 | Board logs `POST -> HTTP 501`, host prints nothing | Python's `SimpleHTTPRequestHandler` implements only `do_GET` and `do_HEAD`. The firmware was correct, the host-side instructions were not |
| 15 | OTA succeeds, but a deliberately broken image never rolls back | `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is off by default, so the app never enters `PENDING_VERIFY` and the rollback call is a no-op |
| 15 | Flash operations fail past 2 MB on a 16 MB part | New ESP-IDF projects default to a 2 MB flash size regardless of the part fitted |
| 17 | Bus-off proof case drowned in hundreds of `bus error detected` lines per second | Logging inside the alert handler. Changed it to increment a counter reported on the periodic stats line, and the bus-off transition became visible |
| 21 | An array write silently corrupting an unrelated struct field | Set a hardware watchpoint on the victim field in GDB. It halted in the producer task and proved index 8 of an 8-element array was aliasing `win_len`: an off-by-one from `<=` instead of `<`. The corrupted value `0x41000000` was IEEE-754 `8.0f` read back through a `uint32_t` |
| 22 | A stripe renderer that appeared to double-buffer but did not | One buffer allocated, filled once, queued repeatedly. It only looked correct because every stripe held identical data. Rebuilt with two DMA-capable buffers and an explicit FREE / FILLING / IN_FLIGHT ownership state released by the `on_color_trans_done` callback |

---

## Verification discipline

Every lab ends with acceptance criteria and the observation that satisfies them. Timing claims are backed by logic analyzer captures or `esp_timer` deltas, not by eyeballing serial output. Where a number is computed rather than measured it is tagged, because "I have used DMA" and "I have configured DMA and measured what it cost me" are different claims and interviewers can tell them apart in one question.

That discipline repeatedly caught errors in my own working material, including a priority-inversion setup whose task delays meant no inversion actually occurred, and continuous-ADC arithmetic that was wrong by 2x because one conversion result is 4 bytes on the ESP32-S3, not 2 (so a 1024-byte frame holds 256 samples, not 512).

---

## Coverage

**Buses:** I2C · SPI · UART · TWAI/CAN · USB-Serial-JTAG
**Timing:** GPTimer · `esp_timer` · LEDC PWM · RMT · fixed-rate sampling and jitter measurement
**Data movement:** DMA (continuous ADC, SPI LCD) · ring buffers · ping-pong buffering · window buffers
**RTOS:** tasks · priorities · core pinning · queues · binary/counting semaphores · mutexes and priority inheritance · ISR-safe primitives · deferred interrupt handling
**Connectivity:** Wi-Fi station · HTTP client · JSON telemetry · BLE GATT (NimBLE) · OTA with rollback
**Storage and power:** NVS · partition tables · wear levelling · deep sleep · RTC retention
**Control and DSP:** PID with anti-windup · derivative-on-measurement · RMS/peak feature extraction · quantized on-device inference
**Tooling:** OpenOCD + GDB over USB-JTAG · hardware watchpoints · core dumps · Unity on-target tests · PulseView logic analysis · clangd + `compile_commands.json`
**Languages:** C (C11) · CMake · Python (host-side tooling)

---

## Environment gotchas

Kept here so they cost you no time.

- **`sdkconfig.defaults` is only read when no `sdkconfig` exists.** Change a default with a `sdkconfig` present and the change is silently ignored. This is why `sdkconfig` is gitignored and `sdkconfig.defaults` is committed.
- **Flash size defaults to 2 MB** in new projects regardless of the part fitted. Set `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` before any OTA or large-asset work.
- **`CONFIG_FREERTOS_HZ` defaults to 100**, which quietly makes `pdMS_TO_TICKS(1)` evaluate to zero. Set it to 1000 explicitly for anything near a 1 kHz sampler.
- **The two USB-C ports are not interchangeable.** The native port is USB-Serial-JTAG (flash, monitor, debug); the other is a UART bridge, needed when the ROM-level reads of on-target Unity tests or a deep-sleep cycle would otherwise drop the console.
- **Never touch GPIO26-32 (SPI flash) or GPIO33-37 (octal PSRAM)** on the N16R8. Strapping pins are 0, 3, 45, 46.
- **Use clangd, not IntelliSense.** IntelliSense squiggles on ESP-IDF headers are cosmetic; the compiler output in the terminal is the truth. Point clangd at `build/compile_commands.json`.

---

## About

**Paul Simbulan** — MSEE candidate, California State University Northridge. BS Computer Science, UC Santa Cruz.

Prior embedded work: bare-metal TM4C123 (Cortex-M4F) firmware at the register level, and TI MSP432P401R. Three MCU families, three toolchains, three interrupt models.

[paulsimbulan.com](https://paulsimbulan.com) · [github.com/pajsimbulan](https://github.com/pajsimbulan) · [linkedin.com/in/pauljsimbulan](https://linkedin.com/in/pauljsimbulan)

MIT licensed. ESP-IDF is Apache 2.0 and Espressif's.
