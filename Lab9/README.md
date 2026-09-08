# Lab 9: SPI Master, Loopback and a Real Slave

The other serial bus: full-duplex framing, clock polarity and phase, chip select as physical addressing, and DMA-backed transfers. Proven twice, first with a wire and then with silicon I did not write both sides of.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8, SPI2_HOST |
| **Pins** | GPIO10 CS, GPIO11 MOSI, GPIO12 SCLK, GPIO13 MISO |
| **Part A** | Loopback, MOSI jumpered to MISO, sending `DE AD BE EF` |
| **Part B** | 74HC595 shift register as a genuine SPI-compatible slave |
| **Key APIs** | `spi_bus_initialize()`, `spi_bus_add_device()`, `spi_device_polling_transmit()`, `spi_transaction_t` |
| **Instrumentation** | FX2LP analyzer, 4 channels at 12 MHz, SPI mode 0 decoder |

---

## Objective

Bring up SPI2 and prove it in two stages. The loopback proves the peripheral is configured correctly with no external device involved. The 74HC595 then proves the bus can drive hardware whose behavior I do not control, which the loopback by construction cannot tell you.

---

## How it works

### CPOL and CPHA both describe the clock

This is the part most explanations muddle. **CPOL** is the clock's idle level: 0 means idle low, 1 means idle high. **CPHA** is more accurately read as *first edge versus second edge* rather than *rising versus falling*, because which physical direction that edge points depends on CPOL. CPHA 0 samples on the first clock edge of each bit period; CPHA 1 samples on the second.

Neither bit says anything about the data lines. Mode 0 (CPOL 0, CPHA 0) is what most devices default to and what this lab uses.

### Full duplex means both directions always

Every clock edge shifts a bit out on MOSI and a bit in on MISO simultaneously. There is no read transaction and no write transaction, only transfers. To read from a device you still have to clock something out, which is why dummy bytes exist.

The `tx` and `rx` arrays in the transaction struct are **RAM buffers**, not shift registers. The shift registers are hardware inside the SPI peripheral; the driver moves bytes between RAM and them.

### The parameters that read as one thing and mean another

- **`max_transfer_sz`** is in **bytes**, not bits. Setting it to 32 permits 32-byte transfers.
- **`queue_size`** is the number of **transactions** that can be queued for the asynchronous API. It is not a ring-buffer byte count and it is not a bit count.
- **`SPI_DMA_CH_AUTO`** lets the driver assign a DMA channel, after which the DMA engine moves bytes between RAM and the peripheral with no CPU involvement per byte. Below a threshold the driver uses CPU copies instead, because for small transfers the DMA setup cost exceeds the transfer.

### SPI versus I2C, which is the actual interview question

SPI has **no addressing and no acknowledge**. Device selection is a physical CS line per device, and the master has no idea whether anything is listening. I2C multiplexes many devices on two wires using transmitted addresses and gets an ACK bit per byte. SPI trades wire count for speed and simplicity; I2C trades speed for wire count and error detection.

The 74HC595 demonstrates the "no acknowledge" half concretely: it takes eight bits, latches them on the CS trailing edge, and lights eight LEDs. It cannot tell you it received them. The ESP-IDF driver's automatic CS toggle happens to produce exactly the latch pulse the chip needs, which is a nice illustration that CS is not merely an enable line. Daisy-chaining two of them sends 16 bits in a single transaction, which drives the point home that the bus has no concept of a device boundary.

---

## TM4C123 bridge

The TM4C's SSI module was configured through `SSICR0` (data size, frame format, and the SPH/SPO bits that are CPHA and CPOL under different names) and `SSICR1`, with the clock divided by `SSICPSR`, and transfers done by writing `SSIDR` and polling `SSISR` for the busy and FIFO flags.

Same peripheral, same four wires, same four modes. What is new here is the transaction abstraction (describe a transfer as a struct, hand it to the driver) and DMA, neither of which the coursework covered.

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| Peripheral configured correctly | Loopback returns `DE AD BE EF` byte-for-byte with the jumper seated |
| Loopback is actually the jumper | Removing it returns `FF FF FF FF`, confirming the read is real and not an echo of the TX buffer |
| Waveform matches configuration | PulseView SPI decoder at 12 MHz resolves CS, SCLK, MOSI, MISO and decodes the same four bytes |
| Real device driven | 74HC595 lights the LED pattern matching the byte written, and the captured waveform matches the pattern |

---

## What broke

**Flat traces on the analyzer, and my first diagnosis was wrong.**
The initial theory was wiring or a missing ground reference, and several rounds of re-probing found nothing because nothing was wrong with the wiring.

The actual problem was **capture window sizing**. A 32 µs SPI burst repeating every 500 ms has a duty cycle of about 0.006 percent. At 12 MHz, 100k samples is an 8.3 ms window, so the odds of that window landing on a burst are tiny and the capture comes back empty almost every time. It is not a signal problem, it is a statistics problem.

Two fixes, both correct: raise the depth to 10M samples for an 833 ms window that is guaranteed to contain a burst, or set a **falling-edge trigger on CS**, which is the efficient answer and the one to reach for from here on.

Worth recording that the wrong diagnosis persisted for several iterations. Learning to ask "is my instrument even capable of seeing this event" before re-checking wiring for the fourth time is the transferable part.

**`FF FF FF FF` from a loopback that should have worked.**
The jumper was not fully seated. An unconnected MISO floats high through the input pull-up, and all-ones is what a disconnected SPI read looks like. Recognizing all-ones and all-zeros as *signatures of absence* rather than as data saves real time.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

Part A: jumper GPIO11 to GPIO13.
Part B: 74HC595 with MOSI to DS (pin 14), SCLK to SHCP (pin 11), CS to STCP (pin 12), powered at 3.3 V.
Analyzer: 12 MHz, 10 M samples or a CS falling-edge trigger.

---

## References

- ESP-IDF Programming Guide v5.5, *SPI Master Driver*
- ESP32-S3 Technical Reference Manual, Chapter 29 (SPI Controller)
- TI SN74HC595 datasheet
