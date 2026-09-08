# Lab 1: GPIO, the RGB LED and the BOOT Button

Digital input and output on the ESP32-S3: configuring pins through the GPIO matrix, reading an active-low button with software debounce, and driving an addressable WS2812 LED whose bit timing is generated in hardware by the RMT peripheral.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8 |
| **Peripherals** | GPIO, RMT (Remote Control Transceiver) |
| **Pins** | GPIO48 onboard WS2812 RGB LED, GPIO0 BOOT button |
| **Parts** | None. Both devices are onboard |
| **Key APIs** | `gpio_config()`, `gpio_get_level()`, `led_strip_new_rmt_device()`, `led_strip_set_pixel()`, `led_strip_refresh()` |
| **Component** | `espressif/led_strip` (managed component, pulled via `idf.py add-dependency`) |

---

## Objective

Read a button and drive an LED. The interesting part is that neither device is as simple as it looks: the button is a strapping pin with active-low wiring and mechanical bounce, and the "LED" is a serial device with a sub-microsecond timing protocol that cannot be bit-banged reliably from a task.

---

## How it works

### Output pin configuration

ESP-IDF configures a pin through a single `gpio_config_t` struct: mode, pin bitmask, pull-up, pull-down, interrupt type. Using designated initializers means any field not named is zero-initialized, which is both concise and a real hazard when a zero value is a meaningful setting rather than a neutral one.

The pin bitmask is `1ULL << pin`, and the `ULL` is not decorative. GPIO48 is beyond bit 31, so a 32-bit shift is undefined behavior and silently configures the wrong pin (or none). The ESP32-S3 uses flat GPIO numbering 0 through 48 rather than the port-and-pin scheme of most Cortex-M parts, so there is no port register to select first.

### Reading an active-low button

The BOOT button pulls GPIO0 to ground when pressed and an internal pull-up holds it high otherwise, so a press reads as logic 0. GPIO0 is also a strapping pin: its level at reset selects the boot mode. Reading it after boot is safe, driving it at reset is not.

Mechanical contacts bounce for a few milliseconds, so a naive edge test fires several times per press. This lab handles it by polling and requiring the level to be stable across consecutive samples separated by a delay. That is the right tool at this stage. Lab 8 replaces it with a hardware edge interrupt that defers the work to a task through a queue, which is the pattern production firmware actually uses.

### Driving the WS2812

The onboard RGB LED is a WS2812 addressable device, not three LEDs on three pins. It takes a single-wire serial stream where a bit is encoded by pulse width, with a 1 and a 0 differing by a few hundred nanoseconds. Toggling a GPIO from a FreeRTOS task cannot hold that tolerance: any interrupt during the stream corrupts the frame.

The **RMT peripheral** solves this. It was designed for infrared remote protocols and generates arbitrary precise pulse trains in hardware from a buffer of symbol descriptors. The CPU writes the color and RMT clocks the waveform out with hardware timing, immune to scheduler jitter.

The `led_strip` driver on top of it uses a framebuffer model: `led_strip_set_pixel()` writes into RAM and changes nothing physically, and `led_strip_refresh()` transmits the buffer. Separating "describe the desired state" from "commit it to the device" is the same pattern that appears again in Lab 22 with a DMA-driven display.

---

## TM4C123 bridge

On the TM4C123, bringing up a pin was five register writes done by hand: enable the port clock in `RCGCGPIO` and wait for `PRGPIO` to acknowledge it, set direction in `GPIODIR`, enable the digital function in `GPIODEN`, and enable the internal pull-up in `GPIOPUR`. Reading was a masked load from `GPIODATA`, whose address bits themselves act as the mask.

`gpio_config()` performs the same work in one call. The clock gating, the direction bit, the digital enable, and the pull configuration all still exist in the ESP32-S3 silicon; the driver writes them for you. The genuinely new hardware concept is the **GPIO matrix**, a crossbar that routes almost any peripheral signal to almost any pin, rather than the TM4C's fixed alternate-function table where a peripheral is only available on the specific pins the datasheet assigned it.

The other new idea is that the LED is a peripheral in its own right with its own protocol, which the TM4C course never had cause to introduce.

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| Output pin configured | LED lights on first refresh after `led_strip_new_rmt_device()` |
| Button reads correctly | Serial log shows level 1 idle, level 0 held while pressed |
| Debounce effective | One state change logged per physical press, not three to five |
| RMT timing correct | Color rendered matches the RGB value written, with no flicker or wrong-color frames |

**Extension completed:** cycled the LED through red, green, and blue on successive presses using a modulo index into a switch statement, which forced a distinction between the *shadow state* held in firmware and the *physical state* of the device. Every later lab that keeps a device model in RAM (NVS, the display, OTA state) relies on the same idea.

---

## What broke

**`led_strip.h` not found, on a project that had built before.**
`managed_components/` and `dependencies.lock` are generated state, and a `fullclean` wipes the first while leaving the second describing dependencies that no longer exist on disk. Deleting both and running `idf.py reconfigure` regenerates them. Related: `idf.py` is only on `PATH` inside the ESP-IDF terminal, so the same command in plain PowerShell fails with something unhelpful.

**Editor errors that were not errors, and a real error I nearly missed.**
clangd flagged ESP-IDF headers it could not resolve while GCC compiled the file without complaint. The reverse also happened: a genuine compile error scrolled past above the editor's cosmetic squiggles. The rule I settled on is that the terminal output is the truth and the editor is a hint, and pointing clangd at `build/compile_commands.json` removed most of the noise. Setting that up once at the user level (`%LOCALAPPDATA%\clangd\config.yaml`) rather than per project saved repeating it in every lab that followed.

**A toggle that never toggled.**
`on != on` instead of `on = !on`. The compiler accepted it because both are valid expressions; only the behavior was wrong. This is the class of bug where reading the code produces a plausible story and only the hardware disagrees.

**A wrong constant passed to `led_strip_clear()`.**
Autocomplete supplied `SOC_MCPWM_TRIGGERS_PER_OPERATOR`, an unrelated SoC capability macro from a different peripheral entirely. It compiled, because it is an integer and the parameter takes an integer. This is a good argument for reading the function signature in the header rather than accepting the first completion, and it is the reason inline AI completions stayed off for the rest of the track.

**Missing semicolons after struct initializers.**
`gpio_config_t btn = { ... }` without the trailing semicolon produces an error message pointing at the *next* line, so the reported location and the actual location differ. Worth internalizing early, because it recurs.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py add-dependency "espressif/led_strip"
idf.py build
idf.py -p COM3 flash monitor
```

---

## References

- ESP-IDF Programming Guide v5.5, *GPIO*, *RMT Transmitter*, and the `led_strip` component documentation
- ESP32-S3 Technical Reference Manual, Chapter 6 (IO MUX and GPIO Matrix), Chapter 27 (RMT)
- WS2812B datasheet, timing specification
