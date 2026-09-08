# Lab 16: Priority Inversion and Mutexes

Reproducing the most-asked RTOS interview topic on real hardware: a high-priority task starved by a low-priority one, measured, then fixed by changing the lock from a binary semaphore to a mutex and measured again.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8 |
| **Peripherals** | None. Three FreeRTOS tasks and a lock |
| **Key APIs** | `xSemaphoreCreateBinary()` vs `xSemaphoreCreateMutex()`, `xTaskNotifyGive()`, `ulTaskNotifyTake()`, `xTaskGetTickCount()` |

---

## Objective

Mutex versus semaphore, priority inversion, and priority inheritance are the three things embedded interviews ask about FreeRTOS more than anything else, and the honest answer is that most people have read about them rather than caused one. This lab causes one deliberately, times it, and fixes it in one line.

---

## How it works

### The three-task scenario

- **Low** priority task acquires the lock and then holds it while doing a long piece of work.
- **Medium** priority task does CPU work and needs no lock at all.
- **High** priority task wants the lock briefly.

High is blocked waiting on Low. That much is normal and expected. The pathology is that **Medium preempts Low** the whole time, because Medium outranks Low and does not need the lock. Low cannot run, so it cannot release, so High stays blocked. The highest-priority task in the system is now waiting on the lowest, gated by a task in the middle that has nothing to do with any of it. The priority scheme has been inverted.

This is not a theoretical concern. The Mars Pathfinder rover kept resetting on the surface in 1997 for exactly this reason, and the fix uploaded from Earth was to enable priority inheritance on the offending mutex.

### The fix

A **mutex** in FreeRTOS implements priority inheritance: when a high-priority task blocks on a mutex held by a lower-priority task, the holder is **temporarily promoted** to the waiter's priority. Low now outranks Medium, runs to completion, releases, and drops back to its original priority. High acquires almost immediately.

A **binary semaphore** does not do this, because it is not an ownership primitive. It has no concept of a holder to promote. That is the actual distinction:

- A **mutex** is owned. It is taken and given by the same task, it supports priority inheritance, and it is for protecting a shared resource.
- A **semaphore** is a counted signal. It can be given by one task or ISR and taken by another, it has no owner and no inheritance, and it is for signalling events or counting resources.

"They're both locks, mutex is binary" is the wrong answer, and it is a common one. This lab is the demonstration that makes the difference concrete rather than memorized. It also retroactively justifies the semaphore usage in Lab 7, where signalling is exactly what was wanted and a mutex would have been wrong.

### Making the inversion deterministic

Getting this to reproduce reliably is harder than it looks and is the reason this lab exists in its current form. An earlier version used tuned `vTaskDelay` values (1000, 1050, 1100 ms) to stagger the tasks. That does not work: the delays meant the high-priority task woke long after the lock had already been released, so it always found the lock free and no inversion occurred at all. The measured wait was zero for both the semaphore and the mutex, and the lab appeared to prove that mutexes make no difference.

The rebuild uses **explicit task notifications** to sequence the three tasks, so the acquire-preempt-block ordering is enforced rather than hoped for. Timing then becomes deterministic across runs instead of dependent on scheduler luck.

The lesson generalizes past this lab: a concurrency demonstration built on sleep durations is not a demonstration, it is a race that happens to usually go one way. If the result does not reproduce identically across many runs, the experiment is not measuring what it claims to.

---

## TM4C123 bridge

There was no scheduler, so there were no task priorities to invert. The nearest analogue is interrupt priority in the NVIC, where a high-priority ISR preempts a low-priority one, but ISRs do not hold locks across preemption and there is nothing to inherit.

Everything here is new territory relative to the coursework, which is precisely why it is in Phase 2.

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| Inversion actually occurs | High-priority task's block duration recorded with `xTaskGetTickCount()` around the take, and it is long, not zero |
| It is caused by Medium | Removing the Medium task collapses the wait, proving Medium is the mechanism rather than Low's work alone |
| Mutex fixes it | Same scenario with `xSemaphoreCreateMutex()` shows the wait dropping to roughly Low's critical-section length |
| Inheritance is visible | Low's effective priority observed rising while High is blocked |
| Result is deterministic | Repeated trials give the same figures rather than a spread |

Both figures are printed by the firmware each run and recorded in the serial log.

---

## What broke

**The first version of this lab proved nothing.**
Covered above: delay-tuned staggering meant the inversion never happened and both configurations measured zero wait. Caught because a result of zero for both cases is not a subtle discrepancy, it is a result that cannot be right, and chasing that rather than accepting it is what turned the lab into a real experiment.

**Sub-tick timing at the default tick rate.**
With `CONFIG_FREERTOS_HZ` at 100, a tick is 10 ms and any measurement of a wait shorter than that quantizes to zero or one tick. `xTaskGetTickCount()` deltas are unusable at that resolution. Either raise the tick rate or measure with `esp_timer_get_time()` in microseconds. Third distinct appearance of the tick-rate issue after Labs 2 and 4.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

The firmware runs the binary-semaphore case and the mutex case in sequence and prints both block durations for direct comparison.

---

## References

- ESP-IDF Programming Guide v5.5, *FreeRTOS* (semaphores and mutexes)
- FreeRTOS API reference: `xSemaphoreCreateMutex`, priority inheritance notes
- Glenn Reeves, "What Really Happened on Mars Rover Pathfinder" (1997)
