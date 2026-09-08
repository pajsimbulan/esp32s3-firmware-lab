# Lab 2: FreeRTOS Tasks and Drift-Free Timing

The paradigm shift from a superloop to a preemptive real-time operating system: independent tasks with their own stacks and priorities, blocking instead of busy-waiting, and the difference between a delay that drifts and a period that does not.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8, FreeRTOS (ESP-IDF port, SMP) |
| **Peripherals** | None. Board only |
| **Key APIs** | `xTaskCreate()`, `vTaskDelay()`, `vTaskDelayUntil()`, `pdMS_TO_TICKS()`, `xTaskGetTickCount()` |
| **Config** | `CONFIG_FREERTOS_HZ` |

---

## Objective

Run two tasks at different rates on the same core and prove they are genuinely concurrent, then make one of them hold an exact period regardless of how long its own work takes. This is the single most important conceptual lab in the track, because every lab after it assumes the RTOS model.

---

## How it works

### Tasks are not functions you call

`xTaskCreate()` allocates a Task Control Block and a **dedicated stack** for the task, then hands the scheduler a new thing to run. Each task is an infinite loop that never returns. The scheduler is preemptive and priority-based: a higher-priority task that becomes ready will interrupt a lower-priority one mid-statement, not politely at a loop boundary.

The consequence that trips people coming from a superloop is that `app_main()` is itself a task, and it is disposable. Once it has spawned the real tasks it can return and the application keeps running, because the scheduler is what is driving execution, not `main`.

Stack size is a real parameter with real failure modes. Too small and you get a stack overflow that presents as corruption in an unrelated variable, which is exactly the bug hunted down with a hardware watchpoint in Lab 21.

### Blocking is not waiting

A busy-wait loop burns 100 percent of a core doing nothing. `vTaskDelay()` moves the task into the blocked state and hands the CPU to whoever else is ready. The task consumes zero cycles while blocked. This distinction (blocking yields the CPU, polling occupies it) is the reason the RTOS exists, and it is the thing interviewers probe when they ask why busy-waiting is a bug.

### Drift

`vTaskDelay(pdMS_TO_TICKS(1000))` means "block for at least 1000 ms **from now**." If the task's own work took 8 ms, the actual period is 1008 ms, and that error accumulates every cycle. Over an hour a 1 Hz heartbeat becomes measurably wrong.

`vTaskDelayUntil()` takes a pointer to the last wake time and delays until an **absolute tick target**, so execution jitter is absorbed rather than added. The period is fixed. This lab runs a 1 Hz heartbeat on `vTaskDelayUntil` and a roughly 4 Hz worker on `vTaskDelay` specifically so the two behaviors sit side by side in the same serial log.

---

## TM4C123 bridge

The ECE 425 model was a superloop with `SysTick` and software delay loops: one thread of control, cooperative by construction, where a long-running function blocks everything else because there is nothing to switch to. Interrupts were the only form of concurrency, and every shared variable was shared with an ISR.

FreeRTOS adds a scheduler between the hardware and the application. The concepts that carry over are the interrupt model and the discipline about shared state. The concepts that are new are per-task stacks, priority-driven preemption, and the idea that a task voluntarily giving up the CPU is the normal case rather than an optimization.

Worth noting for portability: `xTaskCreate`, queues, semaphores, and mutexes are FreeRTOS, not Espressif. That API transfers to STM32, NXP, and TI parts unchanged. `ESP_LOGI` does not.

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| Both tasks run concurrently | Serial log interleaves heartbeat and worker lines rather than alternating in a fixed block |
| Heartbeat period is exactly 1 Hz | Log timestamps 1000 ms apart with no accumulating offset over a long run |
| Worker drifts as expected | `vTaskDelay` period measurably exceeds its nominal value by the task's own execution time |
| Blocked tasks cost nothing | Idle task runs; no watchdog complaints |

---

## What broke

**`vTaskDelay(1)` did not delay 1 ms.**
`CONFIG_FREERTOS_HZ` defaults to **100** in ESP-IDF, not 1000. One tick is 10 ms, so `vTaskDelay(1)` blocks for 10 ms and `pdMS_TO_TICKS(1)` rounds to **zero ticks**, which is a yield rather than a delay. Any delay below 10 ms is quietly impossible at the default tick rate. This is silent: nothing warns you, the code compiles, and the timing is simply wrong. It matters enormously by Lab 4, where a 1 kHz sampler is the requirement, and it is the reason `CONFIG_FREERTOS_HZ=1000` is set explicitly in `sdkconfig.defaults` from this lab onward.

The deeper lesson is that the tick is not a prescaler. A TM4C prescaler trades resolution for range on one timer peripheral and can be tuned per peripheral. The FreeRTOS tick is a system-wide scheduler quantization boundary, and no amount of tuning `vTaskDelay` gets you sub-tick timing. That is what hardware timers are for, which is Lab 4.

**Task stack sizes chosen by guessing.**
Sized generously here, then measured properly later with `uxTaskGetStackHighWaterMark()`. Guessing works until a task uses `printf` with floats, at which point it does not.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

---

## References

- ESP-IDF Programming Guide v5.5, *FreeRTOS (ESP-IDF)* and *FreeRTOS SMP Changes*
- FreeRTOS API reference: `xTaskCreate`, `vTaskDelayUntil`
