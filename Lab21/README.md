# Lab 21: JTAG, OpenOCD and GDB

Halting the target and looking inside it. Breakpoints, hardware watchpoints, RTOS-aware backtraces, and using a watchpoint to catch a memory corruption bug that no amount of added logging would have located.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8, built-in USB-Serial-JTAG on GPIO19/20 |
| **Probe** | None required. The native USB-C port is the debug probe |
| **Stack** | OpenOCD (`board/esp32s3-builtin.cfg`) as GDB server, `xtensa-esp32s3-elf-gdb` as client |
| **Build** | `-Og` optimization while debugging |

---

## Objective

This is the lab most connected to the part of an interview that decides embedded loops. Printf debugging finds bugs whose symptom is near their cause. This one does not.

---

## How it works

### The architecture

OpenOCD speaks JTAG to the target's debug module and exposes a **GDB remote serial protocol server** on a TCP port. GDB connects to that port as a client. Two processes, one connection, and understanding the split is what makes the failure modes diagnosable: OpenOCD failing to claim the device is a USB and driver problem, GDB failing to connect is a port problem, and GDB connecting but showing nonsense is a symbol or optimization problem.

Compile with `-Og`. At `-O2` variables are optimized into registers or eliminated entirely, GDB reports them as "optimized out," and single-stepping jumps around the source in an order that does not correspond to the program you wrote.

### Breakpoints and watchpoints are different hardware

A **breakpoint** halts on execution reaching an address. A **watchpoint** halts on a **data address being accessed**, implemented by the debug unit's address comparators on the memory bus.

That is the capability that matters here. When a variable is being corrupted and you do not know by what, a breakpoint is useless because you do not know where to put it. A watchpoint answers the question directly: it halts at the instant of the write, and the program counter at that moment **is** the culprit.

Hardware watchpoints are a scarce resource, typically two on this part, because each one is a physical comparator.

### RTOS-aware debugging

OpenOCD's ESP32-S3 configuration understands FreeRTOS, so `info threads` lists tasks and `thread apply all bt` gets a backtrace for every one of them. On a system where the bug is a bad interaction between tasks, seeing every task's stack simultaneously at the moment of failure is not a convenience, it is the only view that shows the interaction.

---

## The bug this lab caught

The lab firmware contains a deliberately planted memory corruption bug. Symptom: a struct field held a value nothing in the code ever assigns to it.

```
watch g.win_len
```

The watchpoint halted in `producer_task` at `main.c:27`, on a write to an **array**, not to the field. The producer loop wrote index 8 of an 8-element array. That write landed at address `0x3fc92800`, which is where `win_len` sits, immediately after the array in the struct. Classic off-by-one: `<=` where the bound should have been `<`.

The corrupted value made the diagnosis certain. `win_len` read back as `1090519040`, which as a `uint32_t` is meaningless. As a bit pattern it is `0x41000000`, which is IEEE-754 for `8.0f`. The producer was writing a **float** into the slot past the end of the array, and reading it back through an integer type produced the nonsense value.

Backtrace showed two frames: `vPortTaskWrapper` calling `producer_task`, confirming the corruption happened in normal task context rather than an ISR.

**Why this matters:** the symptom (a wrong integer in a struct field) and the cause (a float array write one element too far) share no source line, no variable name, and no type. Reading the code would not find it, and adding logging around the field would show it changing between two logs without saying who. The watchpoint went straight to the instruction.

Two honest gaps from this session: the fix was identified but not verified by rebuilding with the corrected bound, and `thread apply all bt` was not exercised on a genuine multi-task fault. Both are noted rather than papered over.

---

## The limitation worth knowing

**CPU watchpoints do not catch DMA writes.**

The comparators sit on the CPU's memory interface. DMA is a separate bus master (as established in Lab 20) writing to memory without the CPU issuing those accesses, so a buffer corrupted by a misconfigured DMA descriptor triggers nothing. The watchpoint stays silent and the memory changes anyway.

The alternatives are fill patterns and canaries around the buffer, auditing the DMA descriptor lengths, or memory protection regions. Knowing that your best tool has a blind spot, and knowing exactly where the edge of it is, is more useful than knowing the tool.

---

## TM4C123 bridge

ECE 425 used Keil with an on-board ICDI probe, so breakpoints and single-stepping were available. What was not available was RTOS awareness (there was no RTOS), watchpoints in practice, or a command-line debugger scriptable outside an IDE.

The shift is from a GUI where debugging is buttons to a stack where GDB is a program you drive, which is what a CI-integrated or remote debugging setup requires.

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| OpenOCD claims the target | Both TAPs enumerated, GDB server listening |
| GDB attaches | Connects, halts the target, symbols resolve to source lines |
| Breakpoints work | Halt at a named function, locals readable at `-Og` |
| Watchpoint works | `watch g.win_len` halts at the offending write, not merely afterward |
| Root cause identified | PC at `main.c:27` in `producer_task`, address `0x3fc92800`, value `0x41000000` |
| Backtrace is meaningful | `vPortTaskWrapper` to `producer_task` |

---

## What broke

**`LIBUSB_ERROR_NOT_FOUND` claiming Interface 2.**
OpenOCD could not take the JTAG interface of the composite USB device. The device presents multiple interfaces (serial and JTAG) and Windows had not bound a driver to the JTAG one. Fixed by installing the Espressif USB JTAG driver and rebooting; a reboot was genuinely required, not superstition. After that OpenOCD claimed both TAPs and GDB attached cleanly.

**Wrong USB-C port.**
JTAG is on the **native** port (GPIO19/20), not the UART bridge. Both can be connected at once, which is the useful configuration: flash and monitor over the bridge while debugging over the native port, so a halted target does not cost you the console.

**Variables "optimized out."**
Debugging a `-O2` build. Rebuild at `-Og`. Trivial once you know, and confusing enough to burn twenty minutes if you assume the debugger is broken.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash
openocd -f board/esp32s3-builtin.cfg
# in a second terminal:
idf.py gdb
```

In GDB: `watch g.win_len`, `continue`, then `bt` and `info locals` at the halt.

---

## References

- ESP-IDF Programming Guide v5.5, *JTAG Debugging*, *Configure Other JTAG Interface*, *Debugging Examples*
- OpenOCD user guide, `board/esp32s3-builtin.cfg`
- GDB manual, watchpoints and thread commands
