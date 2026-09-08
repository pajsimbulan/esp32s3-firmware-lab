# Lab 19: On-Device Inference Harness

Running a quantized neural network on the microcontroller itself: fixed-cost feature extraction from the sensor pipeline, a tensor arena sized against real SRAM, and a measured inference latency that has to fit inside the sampling budget.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8, dual LX7 with vector extensions, 8 MB PSRAM |
| **Input** | IMU feature windows from the Lab 7 pipeline |
| **Deployed** | Pre-supplied quantized weights, not a model I trained |
| **The two numbers** | Tensor arena size, inference latency |

---

## Objective

The firmware half of on-device ML. The engineering question is not "can a model classify this," it is **can this device run inference inside its timing and memory budget**, and the answer is two measurements.

---

## Scope, stated plainly

The model weights here were **supplied, not trained by me**. What this lab demonstrates is the deployment and measurement side: getting a quantized model onto the target, wiring the feature extractor to the input tensor, allocating the arena, invoking the interpreter, and measuring what it costs.

That distinction is in this README deliberately. "I trained a model" and "I deployed and profiled a model on an MCU" are different claims, and only one of them is true here. The firmware skills are the transferable ones anyway, and overclaiming the other half is the kind of thing that unravels in a follow-up question.

---

## How it works

### Features, not raw samples

Feeding a 256-sample raw window into a model on a microcontroller is usually the wrong shape. The Lab 7 pipeline already produces RMS and peak per window at **fixed cost**, and fixed cost is the property that matters: a feature extractor whose runtime depends on the data makes the end-to-end latency non-deterministic, which is fatal for a real-time budget.

This is where the classical signal-processing view earns its place. A well-chosen feature set makes a tiny model viable where raw input would need a large one.

### The tensor arena

TensorFlow Lite Micro does **no dynamic allocation**. You hand it one contiguous block, the arena, and it lays out every intermediate tensor inside it at initialization. If the block is too small, allocation fails at init rather than crashing later, which is the correct design for an embedded system.

Sizing it is empirical: start generously, call the interpreter's arena-used-bytes report, shrink to that plus margin. On a part with 8 MB PSRAM this feels unconstrained, but PSRAM is reached over an octal SPI bus through a cache and is meaningfully slower than internal SRAM, so where the arena lives is a performance decision, not just a capacity one.

### Quantization

The model is int8 rather than float32. Four times smaller, and integer MACs are far cheaper than floating-point ones on this class of part. The cost is precision and the need to carry scale and zero-point parameters, so the feature values must be quantized into the input tensor's scale before invocation and dequantized on the way out. Getting that arithmetic wrong produces confident, meaningless output.

### The measurement

`esp_timer_get_time()` either side of `Invoke()`, averaged over many runs. The number that matters is not the average in isolation but its ratio to the window period: at 3.9 windows per second there is roughly 256 ms of budget, and inference has to fit inside it with room for the sampling and the communication path.

---

## TM4C123 bridge

None. 32 KB of SRAM and no vector unit puts this class of workload out of reach on the TM4C123. This lab exists because the ESP32-S3 has the memory and the SIMD extensions to make it plausible, and it is one of the reasons the S3 was the right board for the track.

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| Model loads | Interpreter initializes without an allocation failure |
| Arena sized correctly | Arena-used-bytes reported and the allocation reduced to fit it plus margin |
| Inference runs | `Invoke()` returns success and produces output tensors on real feature input |
| Latency measured | `esp_timer` delta around `Invoke()`, averaged over repeated runs |
| Budget fits | Measured latency compared against the window period, with headroom |

Both figures are printed by the firmware and recorded in the serial log.

---

## What broke

**Quantization parameters applied in the wrong direction.**
Writing feature values into the input tensor without scaling to its quantization parameters produces output that is perfectly well-formed and entirely meaningless. No error, no crash, just numbers. Same failure shape as the Lab 5 two's-complement reassembly: a data-interpretation bug that the type system cannot catch, because every value involved is a valid integer.

**Arena sized by guessing.**
Guessed large, it worked, and "it worked" is not a number. Reading the actual arena usage and reporting that is the difference between a claim and a measurement, and the measurement is the deliverable.

**Scope creep, resisted deliberately.**
The temptation was to build the whole pipeline: collect a dataset, train a model, quantize it, deploy it. That is a project, not a lab, and attempting it would have stalled the track. Splitting it so the firmware half completed with real numbers, rather than leaving the whole thing half-finished, was the right call. The training half remains open and is recoverable later against my own data.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

PSRAM must be enabled in octal mode or large arena allocations fail.

---

## References

- TensorFlow Lite for Microcontrollers documentation, arena allocation and quantization
- ESP-IDF Programming Guide v5.5, *Support for External RAM*, *Heap Memory Allocation*
- ESP32-S3 Technical Reference Manual, vector instruction extensions
