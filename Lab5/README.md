# Lab 5: I2C and IMU Bring-Up

Bringing up a real sensor from its datasheet: probing the bus, reading the device ID, waking the part out of sleep, configuring the full-scale range, and reassembling raw big-endian two's-complement register pairs into signed acceleration in g.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8 |
| **Peripherals** | I2C master (`driver/i2c_master.h`, the v5.x API) |
| **Pins** | GPIO8 SDA, GPIO9 SCL |
| **Device** | TDK InvenSense MPU-6500 6-axis IMU, bus address `0x68` |
| **Key APIs** | `i2c_new_master_bus()`, `i2c_master_bus_add_device()`, `i2c_master_transmit_receive()` |
| **Instrumentation** | FX2LP analyzer, CH0 SDA / CH1 SCL, 4 MHz, 1 M samples, I2C decoder |

---

## Objective

This is the core of any sensor project and the most-mined lab in the track for interview material. The deliverable is a working driver written against the register map, not a call into someone else's sensor library.

---

## How it works

### Addressing is on the wire, not on pins

The `0x68` device address is a **7-bit value clocked out serially on SDA** in the first frame after the start condition, followed by a read/write bit. It is not a pin configuration and not a chip-select line. A single pair of wires can carry a dozen devices because each one compares that transmitted address against its own and only the match responds. The AD0 pin on the breakout changes the device's address to `0x69`, which is the only sense in which a pin is involved.

This is the fundamental contrast with SPI in Lab 9, where there is no address at all and selection is a physical CS line per device.

### The v5.x driver model

ESP-IDF v5 replaced the old `i2c_cmd_link` command-list API with a two-level handle model: a **bus handle** representing the controller and its pins, and a **device handle** per attached device carrying that device's address and clock speed. `i2c_master_transmit_receive()` performs a write-then-repeated-start-then-read in one call, which is exactly the register-read pattern every I2C sensor uses.

### Bring-up sequence

1. **Probe.** Confirm something acknowledges at `0x68`. An address with no ACK is a wiring problem, not a code problem.
2. **`WHO_AM_I` (register `0x75`).** Read the device's identity constant. This is the checkpoint that proves reads work before anything else is attempted.
3. **Wake (register `0x6B`).** The part boots into sleep mode. Clearing the sleep bit in `PWR_MGMT_1` is required, and skipping it yields plausible-looking constant zeros.
4. **Configure (register `0x1C`).** Accelerometer full-scale range, which sets the LSB-per-g scale factor used later.
5. **Burst read (register `0x3B`).** Six bytes in one transaction covering all three axes, rather than six separate transactions. On a shared bus the difference is not cosmetic.

### Reassembling the data

Each axis is two registers, high byte first, forming a **big-endian signed 16-bit two's-complement** value. The correct assembly is `(int16_t)((hi << 8) | lo)` with the cast applied to the combined value, so the sign bit is interpreted once at the right width. Combining into an `int` and casting afterward, or assembling little-endian, both produce numbers that look like data and are wrong.

Dividing by the scale factor for the configured range gives g. The sanity check is gravity: a stationary board should read close to 1.0 g on whichever axis is vertical.

---

## TM4C123 bridge

ECE 425 drove I2C through the `I2CMSA`, `I2CMDR`, and `I2CMCS` registers by hand, one byte per transaction, with the START / RUN / STOP / ACK control bits written explicitly for each phase and `I2CMCS` polled for BUSBSY between steps. A repeated start was a specific bit combination you had to know.

The ESP-IDF driver builds that state machine for you, but the sequence it generates is identical, and being able to point at a captured waveform and name the start, address frame, ACK, repeated start, and stop is what the register-level experience buys.

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| Device present on the bus | Probe ACKs at `0x68` |
| Reads work | `WHO_AM_I` returns `0x70` consistently |
| Part is awake | Register values change with movement instead of holding at zero |
| Bus speed as configured | PulseView measured SCL at **~399 kHz** against a 400 kHz configuration |
| Data is physically correct | Stationary board reads **~0.993 g** on the vertical axis; noise floor **~0.002 g rms** |
| Sign handling is correct | Inverting the board flips the sign cleanly rather than wrapping through a large positive value |

---

## What broke

**`WHO_AM_I` returned `0x70` when every tutorial says `0x68`.**
The breakout is a **GY-9250/GY-6500 style board carrying an MPU-6500**, not the MPU-6050 the ecosystem assumes. `0x70` is the MPU-6500's device ID. (MPU-9250 returns `0x71`, MPU-9255 returns `0x73`.) All of them answer at bus address `0x68`, which is a completely separate number and the source of the confusion. Every register this lab touches (`0x6B`, `0x3B`, `0x1C`) is identical across the family, so no driver code changed.

The reason this is worth recording rather than being embarrassed about: reading `0x70` means the wiring is **correct**. Treating an unexpected but consistent value as evidence rather than as failure is the actual skill, and the resolution came from the TDK register map, not from a forum.

**The bus was running at 100 kHz while the code said 400 kHz.**
Did not surface here. It surfaced in Lab 7 as a sample rate four times slower than expected, and took a GPIO-toggle capture to find. Documented in full in that lab.

**Analyzer showed nothing at first.**
I2C at 400 kHz needs at least 4 MHz sampling to resolve edges cleanly; 1 MHz gives only 10x oversampling and the transitions get chunky enough to confuse the decoder. Common ground is mandatory.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

Wiring: VCC to 3V3, GND to GND, SDA to GPIO8, SCL to GPIO9. The breakout carries its own pull-ups.

---

## References

- TDK InvenSense MPU-6500 Register Map and Descriptions (rev 2.0)
- ESP-IDF Programming Guide v5.5, *I2C Master*
- ESP32-S3 Technical Reference Manual, Chapter 22 (I2C Controller)
- NXP I2C-bus specification UM10204
