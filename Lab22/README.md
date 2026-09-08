# Lab 22: SPI TFT with DMA Ping-Pong Buffering

Driving a 240x320 ST7789 display over SPI with DMA, using two buffers and an explicit ownership invariant so the CPU fills one stripe while the DMA engine transmits the other. The buffer handoff is released from an ISR callback and measured in frames per second.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8, SPI2_HOST with DMA |
| **Display** | ST7789, **240x320**, 16-bit RGB565, write-only (no MISO) |
| **Pins** | GPIO12 SCLK, GPIO11 MOSI, GPIO10 CS, GPIO14 DC, GPIO15 RST |
| **Clock** | 10 MHz pixel clock, SPI mode 0 |
| **Buffers** | 2 x DMA-capable, one 40-row stripe each (240 x 40 x 2 bytes) |
| **Key APIs** | `esp_lcd_new_panel_io_spi()`, `esp_lcd_new_panel_st7789()`, `esp_lcd_panel_draw_bitmap()`, `on_color_trans_done`, `xSemaphoreCreateCounting()` |

---

## Objective

The most demonstrable thing in the repository, and the one that pulls together SPI from Lab 9, DMA from Lab 20, and the ISR-to-task handoff from Labs 7 and 8 into a single pipeline with a measurable throughput number.

---

## How it works

### Why two buffers

A single buffer forces serialization: fill it, transmit it, wait for the transmit to finish, fill it again. The CPU idles during the transmit and the DMA engine idles during the fill, so the frame rate is the **sum** of both costs.

Ping-pong buffering overlaps them. While the DMA engine transmits buffer A, the CPU fills buffer B. The frame rate becomes the **maximum** of the two costs rather than the sum, and which one dominates tells you what to optimize.

### The ownership invariant

Each buffer is in exactly one of three states: **FREE**, **FILLING** (owned by the CPU), or **IN_FLIGHT** (owned by the DMA engine). The invariant is that the CPU must never write a buffer that is IN_FLIGHT, and it is enforced with a **counting semaphore** initialized to the buffer count:

- Before filling, the task takes the semaphore. If both buffers are in flight it blocks, which is correct backpressure rather than a stall to work around.
- `esp_lcd_panel_draw_bitmap()` queues the transfer and returns immediately. It does not wait.
- When the transfer completes, `on_color_trans_done` fires **in ISR context** and gives the semaphore with `xSemaphoreGiveFromISR()`, returning a value to `portYIELD_FROM_ISR` if a higher-priority task was woken.

That is the same ISR-gives, task-takes signalling pattern as Lab 7, applied to buffer ownership rather than to a sample tick. If `draw_bitmap` returns an error the semaphore is given back immediately, or the buffer leaks and the pipeline deadlocks after two frames.

### DMA-capable memory

`heap_caps_malloc(..., MALLOC_CAP_DMA | MALLOC_CAP_8BIT)` rather than plain `malloc`. The DMA engine can only reach certain memory: internal SRAM with the right alignment, not arbitrary heap and not PSRAM without extra handling. A regular `malloc` may return memory the DMA controller cannot address, and the failure is a transfer that does nothing rather than an allocation error.

### Stripes, not frames

A full 240x320 frame at 16 bpp is 150 KB, and two of them is most of the available internal SRAM. The display is instead written in **40-row stripes** of 19.2 KB each, eight per frame. Buffer memory drops by 8x, latency to first visible output drops, and the tradeoff is more transactions and more interrupts. This is exactly the same frame-size tradeoff as the DMA ADC in Lab 20, arrived at from the other direction.

### Instrumentation

The firmware times the fill phase separately from the total frame time and reports fps, cumulative fill milliseconds, and the ISR completion count every 60 frames. Separating fill time from total time is what makes the result diagnostic rather than decorative: if fill time approaches frame time, the CPU is the bottleneck and the overlap is buying nothing.

---

## What broke

**A "double-buffered" implementation that had one buffer.**

An earlier version allocated a single buffer, filled it once, and queued it repeatedly, crediting `.trans_queue_depth` for an overlap that parameter does not provide. Queue depth controls how many transactions can be *pending*; it does nothing about who owns the memory those transactions point at.

It appeared to work perfectly. The reason is the thing worth remembering: **every stripe held identical data**, so reusing a buffer that was still in flight produced exactly the correct image. The bug was invisible because the test case could not distinguish correct behaviour from broken behaviour.

The rebuild allocates two DMA-capable buffers with the explicit ownership state above, and the verification case renders a **different colour per stripe**, so stale reuse would show as a visibly wrong band rather than as nothing at all. Designing the test so the failure is visible was the actual fix; the second buffer was just the code.

**The display is 240x320, not 240x240.**
My reference material assumed the square variant. Wrong `V_RES` means the address window is set incorrectly and the image is offset or clipped, and depending on the panel's memory layout it may also need an offset correction in the init sequence. Verify the panel's actual resolution against the panel, not against a document.

**Colours inverted.**
Many ST7789 panels ship expecting inverted colour, so `esp_lcd_panel_invert_color(panel, true)` is required. Without it, everything is a photographic negative, which looks like a pixel-format bug (RGB565 byte order, RGB versus BGR element order) and is not.

**`miso_io_num = -1` is deliberate.**
The display is write-only and there is no MISO wire. Setting it to `-1` tells the driver not to allocate the pin. Leaving a real pin number configured wastes a GPIO and can produce contention. Same for `quadwp` and `quadhd`.

**`max_transfer_sz` sized to the wrong thing.**
It has to cover **one stripe** in bytes (240 x 40 x 2 = 19200), not one row and not one frame. Too small and the driver splits or rejects the transfer; the parameter is in bytes, which is the same trap as Lab 9.

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| Panel initializes | ST7789 init sequence completes, backlight on, no error from any `ESP_ERROR_CHECK` |
| Buffers are DMA-capable | Both allocations succeed under `MALLOC_CAP_DMA`, size logged at startup |
| Overlap is real | ISR completion count tracks frames; the task blocks on the semaphore rather than spinning |
| Ownership invariant holds | Per-stripe distinct colours render correctly, so no stale buffer is reused |
| Throughput measured | fps and cumulative fill time reported every 60 frames from `esp_timer` deltas |
| Image renders correctly | A converted bitmap displays right-side-up, correct colours, no tearing or banding |

---

## Extra

`convert.py` in this folder converts a source image to an RGB565 C array (`cat_img.h`) for compiling into flash. Worth knowing that a 240x320 16-bit image is 150 KB of `.rodata`, which is a real fraction of the binary and a good concrete illustration of why display assets on embedded targets get compressed or stored in a separate partition.

---

## TM4C123 bridge

ECE 425 drove a character LCD over parallel GPIO with hand-timed strobes and CPU-driven byte writes. Same fundamental idea (command versus data selection, an init sequence from a datasheet, an address window) at a completely different scale: a 16x2 character display versus 76,800 pixels at 16 bits, which is what forces DMA into the picture.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

PSRAM enabled in octal mode. Wiring per the pin table above, plus 3V3 and GND.

---

## References

- ESP-IDF Programming Guide v5.5, *LCD*, *SPI Master Driver*, *Heap Memory Allocation* (`MALLOC_CAP_DMA`)
- Sitronix ST7789 datasheet, init sequence and address window commands
- ESP32-S3 Technical Reference Manual, GDMA and SPI chapters
