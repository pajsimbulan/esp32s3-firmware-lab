# Lab 3: UART, Loopback, and a Serial Command Parser

Asynchronous serial from framing up: baud rate and its error budget, the driver's ring buffers, a physical loopback to prove the peripheral works before trusting it, and a line-oriented command parser handling single-byte arrivals.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8 |
| **Peripherals** | UART1, USB-Serial-JTAG, RMT (for the LED the parser drives) |
| **Pins** | GPIO17 TX, GPIO18 RX, joined by one jumper for loopback |
| **Key APIs** | `uart_driver_install()`, `uart_param_config()`, `uart_set_pin()`, `uart_write_bytes()`, `uart_read_bytes()`, `ESP_LOGx` |
| **Instrumentation** | FX2LP analyzer, 4 MHz, UART 115200 8N1 decoder |

---

## Objective

Bring up a UART, prove it end to end with a TX-to-RX jumper, decode the line on a logic analyzer, and then build something on top of it: a parser that accepts `led on`, `led off`, and `led status` over serial and drives the onboard RGB LED accordingly.

The loopback matters more than it looks. It answers "is the peripheral configured correctly" without a second device in the picture, which means that when a real device later fails to talk, the peripheral is already eliminated as a suspect. That pattern (prove the bus alone, then add the device) repeats in Lab 9 for SPI and Lab 17 for CAN.

---

## How it works

### Framing and baud

UART has no clock line. Both ends agree on a bit rate in advance and the receiver recovers timing from the falling edge of the start bit, then samples each bit at its nominal center. 8N1 means eight data bits, no parity, one stop bit: ten bit-times per byte including framing, so 115200 baud carries 11520 bytes per second, not 14400.

Because the receiver free-runs after the start bit, accumulated timing error across ten bits is what determines whether a frame decodes. Roughly 2 percent total error is the practical ceiling, which is why baud rates are chosen as clean divisions of the peripheral clock rather than arbitrary numbers.

### The driver owns two ring buffers

`uart_driver_install()` allocates a TX and an RX ring buffer and installs an ISR. From that point the application is not touching the hardware FIFO: `uart_write_bytes()` copies into the TX ring and returns, and the ISR drains it. `uart_read_bytes()` pulls whatever the ISR has accumulated in the RX ring, blocking up to a timeout.

This is a real architectural difference from polling a status flag, and it is why the UART keeps working while a task is busy elsewhere.

### Two consoles, one board

The board presents a console over the native **USB-Serial-JTAG** peripheral and separately over **UART0** on GPIO43/44. Both are live at once. During this lab the boot log came out of UART0 while the command parser read from USB-Serial-JTAG, which looks like a bug until you know there are two independent paths.

### Parsing a line that arrives one byte at a time

Keystrokes arrive individually, so the parser cannot assume a complete line per read. It keeps an accumulation buffer and an index that **survives across loop iterations**, appending until it sees `\r` or `\n`, then null-terminating and dispatching. A terminal sending `\r\n` fires the terminator twice, so an empty-line guard is required or every command dispatches a phantom second time.

Typed characters are also not echoed. Serial terminals show what you type because of **local echo** in the terminal, or because the remote device echoes it back. The device controls only the second one, and adding it is a single write of the received byte.

---

## TM4C123 bridge

On the TM4C the baud divisor was computed by hand: an integer part into `UARTIBRD` and a fractional part into `UARTFBRD`, derived from the system clock and the target rate, then line control set in `UARTLCRH` and the port enabled in `UARTCTL`. Transmitting meant polling `UARTFR` for the TX-FIFO-full flag and writing `UARTDR` one byte at a time.

Here `uart_param_config()` takes a baud rate as an integer and does the divisor arithmetic itself, and the driver replaces the polling loop with an interrupt-fed ring buffer. The register-level intuition still pays off: knowing that a divisor and a fractional divisor exist underneath is what makes baud error a concept rather than a mystery.

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| Loopback returns what was sent | Transmitted pattern read back byte-for-byte with the jumper in place, and times out without it |
| Framing is correct on the wire | PulseView UART decoder at 115200 8N1 resolves the transmitted ASCII on GPIO17 |
| Parser handles fragmented input | Characters typed one at a time assemble into a single dispatched command |
| Commands act on hardware | `led on` lights the WS2812 green, `led off` clears it, `led status` reports the shadow state correctly |

---

## What broke

**`LINE_MAX` silently collided with a system header.**
A buffer size constant named `LINE_MAX` clashes with the definition in `syslimits.h`, pulled in transitively. Renamed to `CMD_LINE_MAX`. This is the argument for prefixing project-scope macros, and a good example of a bug whose error message points nowhere near its cause.

**`usb_serial_jtag_config_t` does not exist.**
The type is `usb_serial_jtag_driver_config_t`. Close enough to autocomplete confidently, different enough not to compile.

**A null handle crash that looked like a driver bug.**
`led_init()` configured the LED strip but never called `led_strip_new_rmt_device()`, so the handle stayed null and the first `set_pixel` faulted. The panic pointed at the driver. The missing line was mine.

**`led_strip` v2.5.5 does not have the fields the newer docs show.**
`.color_component_format` and `LED_STRIP_COLOR_COMPONENT_FMT_GRB` are not present in the resolved component version. Omitting them entirely works, because designated initializers zero-initialize the rest and the defaults match. Lesson: check the version you actually resolved, not the documentation you happened to find.

**A `1024` buffer that was typed as `10243`.**
Compiled fine. Allocated ten times the intended size. Found by reading the line rather than by any tool.

**Editor squiggles on every ESP-IDF header.**
Fixed permanently by creating a **user-level** clangd config at `%LOCALAPPDATA%\clangd\config.yaml` with `CompileFlags: CompilationDatabase: build`, rather than dropping a `.clangd` file into every project. Applies to all labs from here on.

**A new project that would not build, then would not flash.**
The standard new-project failure chain: wrong chip target until `idf.py set-target esp32s3`, then a missing `.elf` because `set-target` reconfigures without compiling, then a missing `managed_components/` until `idf.py add-dependency` was rerun and the project rebuilt.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py add-dependency "espressif/led_strip^2.5.5"
idf.py build
idf.py -p COM3 flash monitor
```

Jumper GPIO17 to GPIO18 before running the loopback case.

---

## References

- ESP-IDF Programming Guide v5.5, *Universal Asynchronous Receiver/Transmitter (UART)* and *USB Serial/JTAG Controller Console*
- ESP32-S3 Technical Reference Manual, Chapter 26 (UART Controller)
