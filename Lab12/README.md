# Lab 12: Deep Sleep and Power Budgeting

Putting the SoC into its low-power states, choosing what wakes it, keeping a small amount of state alive across the sleep boundary, and measuring the current draw with an instrument rather than estimating it from a datasheet.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8 |
| **Subsystem** | RTC controller, deep sleep, RTC slow memory |
| **Key APIs** | `esp_sleep_enable_timer_wakeup()`, `esp_sleep_enable_ext0_wakeup()`, `esp_deep_sleep_start()`, `esp_sleep_get_wakeup_cause()`, `RTC_DATA_ATTR` |
| **Measurement** | Digital multimeter in series on the 5 V supply |

---

## Objective

Every battery-powered sensor node lives or dies on this lab. Get the board into deep sleep, wake it on a timer, prove that a counter survived, and put a real number on what it costs while asleep.

---

## How it works

### Deep sleep is a reboot with a memory

The CPU cores, most of the SRAM, and the peripherals are powered down. What stays alive is the RTC domain: a low-power controller, its own oscillator, and a small block of RTC slow memory. On wake, execution does **not** resume where it left off. The chip runs the bootloader again and enters `app_main()` from the top, exactly like a reset.

The practical consequence is that ordinary globals are gone. Anything that must survive is declared `RTC_DATA_ATTR`, which places it in RTC slow memory that stays powered. The wake counter in this lab is the demonstrator: it increments across sleep cycles instead of resetting to zero.

`esp_sleep_get_wakeup_cause()` is how the application distinguishes a cold boot from a wake, and which wake source fired. That branch at the top of `app_main()` is the whole structure of a duty-cycled device: cold boot does full initialization, wake does the minimum, takes a reading, and goes back to sleep.

### Wake sources

Timer wake is the RTC counter reaching a deadline. GPIO wake (`ext0` for a single pin, `ext1` for a mask of pins with an AND or OR condition) requires an RTC-capable pin, because the main GPIO matrix is powered down. That constraint is the one that catches people: not every pin can wake the chip, and the pin you picked for convenience in an earlier lab may not be eligible.

Light sleep is the middle option, where the CPU is halted but state is retained and execution genuinely resumes in place. Higher current, no re-initialization cost.

### Reading the boot ROM message

The first line out of the serial port after a wake reads `rst:0x5 (DSLEEP)`. That reset reason code is direct confirmation from the ROM that the chip came out of deep sleep rather than being reset or browned out. Learning to read the boot header rather than skipping past it is a small habit that pays off constantly.

---

## Measurement

Current was measured with a **DMM in series on the 5 V supply**, which is the only way to get a real number. A dev board measures far higher than a bare module would, because the USB-to-serial bridge, the LDO regulator, and the RGB LED are all still drawing. The measurement is honest about what it includes, which is the point: a power budget built from datasheet typicals rather than a measurement is a guess.

The battery-life arithmetic that follows from it is straightforward, and being able to do it out loud (average current from duty cycle, divide capacity by it, subtract self-discharge) is a standard question for anyone claiming battery-device experience.

---

## TM4C123 bridge

The TM4C had sleep and deep-sleep modes driven by the `SYSCTL` clock gating registers, entered with a `WFI` instruction, waking on any enabled interrupt. Execution resumed from the instruction after `WFI` with all state intact, which makes it closer to light sleep than deep sleep here.

The concept genuinely new on the ESP32-S3 is that deep sleep is a **power domain shutdown**, not a CPU halt, and therefore the application must be structured around losing its own memory. The RTC domain and the `RTC_DATA_ATTR` retention model have no TM4C equivalent.

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| Chip enters deep sleep | Serial output stops, current drops sharply on the DMM |
| Timer wake fires | Device re-boots on schedule with `rst:0x5 (DSLEEP)` in the boot header |
| State survives | `RTC_DATA_ATTR` wake counter increments rather than resetting |
| Wake cause is identifiable | `esp_sleep_get_wakeup_cause()` reports the timer source, and cold boot reports none |
| Sleep current measured | DMM reading recorded in series on 5 V, with the caveat that it includes the whole dev board |

---

## What broke

**The serial monitor died every sleep cycle and would not come back.**
The native USB-Serial-JTAG peripheral is part of the powered-down domain, so the COM port **disappears from the host** the moment the chip sleeps and re-enumerates on wake. Windows treats that as a device removal, the monitor session dies, and reconnecting by hand every cycle makes the lab unusable.

The fix is the **UART bridge port**, which is a separate chip on the board and stays enumerated regardless of what the ESP32-S3 is doing. Same distinction that mattered for Unity's interactive menu in Lab 11, hit from a completely different direction.

**A wake pin that could not wake anything.**
GPIO wake requires an RTC-capable pin. Picking a pin for wiring convenience and discovering afterward that it is not in the RTC domain is a five-minute problem if you know the constraint exists and an hour if you assume the API is broken.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM4 flash monitor    # UART bridge port survives the sleep cycle
```

For current measurement, break the 5 V line and put the DMM in series before starting.

---

## References

- ESP-IDF Programming Guide v5.5, *Sleep Modes*
- ESP32-S3 Technical Reference Manual, Chapter 9 (Low-Power Management)
- ESP32-S3 datasheet, current consumption tables
