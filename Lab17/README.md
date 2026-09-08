# Lab 17: CAN Bus (TWAI)

The bus that runs vehicles, industrial machinery, and a lot of aerospace. Message-ID broadcast instead of addressing, non-destructive bitwise arbitration, hardware acceptance filters, error counters, and the bus-off state.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8, TWAI controller |
| **Pins** | GPIO5 TX, GPIO4 RX |
| **Part A** | Self-test loopback, `.self = 1`, no external hardware |
| **Part B** | Real differential bus: SN65HVD230 transceivers, 2x 120 Ω termination |
| **Bit rate** | 500 kbit/s |
| **Key APIs** | `twai_driver_install()`, `twai_start()`, `twai_transmit()`, `twai_receive()`, `twai_read_alerts()`, `twai_get_status_info()` |

---

## Objective

CAN is a named question at SpaceX and across the automotive and defense tiers, and it is a bus almost no student project touches. Espressif calls its controller **TWAI** (Two-Wire Automotive Interface) for trademark reasons, but `twai_*` is CAN.

---

## How it works

### Messages are broadcast, not addressed

There is no destination. A frame carries an **identifier** describing what the message *is* (engine RPM, brake pressure) rather than who it is for. Every node sees every frame and decides for itself whether it cares. Adding a listener requires no change to the transmitter, which is why the bus scales.

### Arbitration is non-destructive

The bus is wired-AND: a dominant bit (0) wins over a recessive bit (1). Every transmitter monitors the bus while sending its ID, and any node that sends recessive while reading dominant knows it lost and stops immediately. The winner's transmission is **not corrupted** and continues without a retry. Lower ID numerically means higher priority.

Compare with Ethernet's CSMA/CD, where a collision destroys both frames and both parties back off randomly. CAN resolves contention deterministically with no wasted bandwidth, which is why it is used where latency has to be bounded.

### Why self-test mode exists

Every CAN frame must be **acknowledged by at least one other node**, which writes a dominant bit in the ACK slot. A lone controller with nothing else on the bus therefore errors out on every transmission and eventually goes bus-off. `.self = 1` (self-test mode) relaxes the ACK requirement so a single board can prove its own peripheral, with TX jumpered to RX. Same "prove the peripheral before adding the device" discipline as the UART loopback in Lab 3 and the SPI loopback in Lab 9, but here it exists because of a protocol property rather than as a convenience.

### Error states

CAN nodes maintain transmit and receive error counters. Above 127 a node becomes **error-passive** and stops asserting dominant error flags. Above 255 it goes **bus-off**, disconnects itself from the bus entirely, and requires an explicit recovery. This is a fault-containment mechanism: a node with a broken transceiver babbling on the bus takes itself out rather than jamming everyone. `twai_initiate_recovery()` is how it comes back.

### Termination

Exactly **two 120 Ω resistors, at the two physical ends of the bus**, regardless of how many nodes are on it. This is a transmission-line requirement, not a per-node one. Three nodes still means two resistors. Getting this wrong produces reflections and intermittent errors that look like software bugs.

---

## Proof cases

| # | Case | Result |
|---|---|---|
| 1 | Self-test loopback with an incrementing counter byte | Frames received back as `DE AD BE 00` through `DE AD BE 0E`, counter incrementing, proving the peripheral transmits and receives |
| 2 | Hardware acceptance filter | Frames sent with ID `0x456` against a filter configured for a different ID: `tx_ok` climbs, `rx_ok` stays at zero. The controller silently drops non-matching IDs without waking the CPU |
| 3 | Bus-off | `TWAI_MODE_NORMAL` with no second node to ACK: error counters climb past 255 and the node transitions to bus-off, confirmed through `twai_get_status_info()` |
| 4 | Part B, real differential bus | Two nodes through SN65HVD230 transceivers with 120 Ω at each end, exchanging frames with no error-counter growth |

Case 2 is the one worth understanding rather than just running. The filter is in **hardware**: the frame is received by the controller, compared against the acceptance mask, and discarded before any interrupt fires. On a busy vehicle bus carrying thousands of frames per second, that is the difference between a CPU that can do its job and one that spends all its time rejecting other people's traffic.

---

## TM4C123 bridge

The TM4C123 has a CAN controller, and ECE 425 never touched it. No bridge to draw. The nearest concept from the coursework is UART framing, and CAN's differential signalling, arbitration, and error confinement are all past it.

---

## What broke

**The bus-off proof case drowned in its own logging.**
`twai_read_alerts()` reports `TWAI_ALERT_BUS_ERROR`, and in `TWAI_MODE_NORMAL` with no ACKing node that alert fires on essentially every bit time. Logging inside the alert handler produced **hundreds of lines per second**, and the actual bus-off transition scrolled past invisibly in the flood.

Fix: count the errors instead of logging them, and report the count on the existing periodic stats line. The transition became immediately visible.

This is a real production pattern rather than a lab workaround. Log volume in an error path is a design decision, and a handler that logs per-event on a fault that fires at bus rate is a denial of service against your own console. The general rule is that fault handlers count and rate-limit; they do not narrate.

**The SN65HVD230 RS pin.**
Breakout boards vary in how the slope-control / standby pin is handled, and left in the wrong state the transceiver appears dead with no error anywhere: the controller reports transmissions succeeding into a bus that carries nothing. A silent failure at the layer below the one you are debugging, which is the worst kind.

**MCP2551 is a 5 V part.**
Frequently sold as an interchangeable CAN transceiver and frequently mislabelled in listings. The ESP32-S3's GPIO pins are **not 5 V tolerant**, so it can damage the board. SN65HVD230 is the 3.3 V part. Checked before wiring rather than after.

**Six defects in my own first implementation.**
Enum name typos, a stray character, a missing include, an unused variable warning, and two behavioural bugs: a missing counter variable that made the payload constant so loopback appeared to work while proving nothing, and a missing trailing space in a `sprintf` format string that truncated the RX log. The constant-payload one is the instructive one, because it produced output that looked exactly like success.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

Part A: jumper GPIO5 to GPIO4, `.self = 1`.
Part B: SN65HVD230 on each node, CANH to CANH, CANL to CANL, common ground, 120 Ω across CANH/CANL at both physical ends.
Analyzer: CH0 GPIO5, CH1 GPIO4, 10 MHz, CAN decoder at 500 kbit/s.

---

## References

- ESP-IDF Programming Guide v5.5, *Two-Wire Automotive Interface (TWAI)*
- ESP32-S3 Technical Reference Manual, Chapter 33 (TWAI Controller)
- Bosch CAN Specification 2.0, ISO 11898
- TI SN65HVD230 datasheet
