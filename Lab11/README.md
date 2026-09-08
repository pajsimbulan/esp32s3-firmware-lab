# Lab 11: On-Target Unit Testing with Unity

Running an actual test suite on the microcontroller: a test runner in flash, an interactive menu over serial, assertions that fail loudly, and the discipline of separating logic that can be tested from hardware that cannot.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8 |
| **Framework** | Unity (bundled with ESP-IDF) |
| **Console** | UART bridge port, not the native USB-Serial-JTAG port |
| **Key APIs** | `TEST_CASE()`, `TEST_ASSERT_*`, `unity_run_menu()` |
| **Build** | `idf.py build flash monitor` on a `test/` component with `WHOLE_ARCHIVE` |

---

## Objective

Move from "I flashed it and it looked right" to a repeatable, automated verification pass. This is the lab that separates a hobby project from firmware, and it is the one that most student repositories skip entirely.

---

## How it works

### The tests run on the target, not on the host

Unity is compiled into the firmware image and executed on the ESP32-S3 itself. That matters because a host-run test would use the host's integer widths, alignment rules, endianness, and floating-point behavior. Testing on the target means testing against the actual `int` width, the actual stack, the actual compiler, and the actual optimizer.

`TEST_CASE("description", "[tag]")` registers a test through a linker section constructor. `unity_run_menu()` presents them interactively over serial, so you can run one test, a tagged subset, or everything.

### Testing what can be tested

Hardware-dependent behavior is hard to unit test without a fixture. What is straightforward, and what most of the bugs actually live in, is the **pure logic** underneath:

- The two's-complement reassembly and scaling from Lab 5. Feed it known byte pairs, assert the resulting g value. No I2C required.
- The RMS and peak computation from Lab 7. Feed it a known array, assert the result against a hand-computed value.
- Ring buffer and window index arithmetic. The wrap case at the boundary is where off-by-one errors live, and it is trivially testable and almost never tested.

Pulling that logic into functions that take arrays and return numbers, rather than leaving it inline in a task that also does I2C, is a refactor that testing forces and that improves the code independently. That is the real argument for unit tests on embedded work, and it is a better interview answer than "tests are good practice."

### Assertions that mean something

`TEST_ASSERT_EQUAL_INT`, `TEST_ASSERT_FLOAT_WITHIN`, `TEST_ASSERT_EQUAL_HEX8`. The typed variants exist so the failure message prints the expected and actual values in a readable form. `TEST_ASSERT_FLOAT_WITHIN` specifically exists because exact float equality is almost always the wrong assertion.

---

## TM4C123 bridge

There was no equivalent. Verification in ECE 425 was flashing the board and watching an LED or a serial line, which is a real test in the sense that it exercises the system, and no test at all in the sense that it is manual, unrepeatable, and only checks the one case you thought to try.

The concept being introduced here is not new to me from a software background; what is new is applying it under embedded constraints, where the test runner has to fit in flash, run without an OS underneath it, and report over a serial line.

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| Test menu appears | Serial console lists registered tests and accepts a selection |
| Tests pass | Known-good inputs produce the expected results with no assertion failures |
| Tests actually fail when they should | Deliberately broke a scale factor and confirmed the assertion caught it with a useful message |
| Suite is repeatable | Same result across resets, no ordering dependence between tests |

---

## What broke

**The test menu came up completely empty.**

This is the failure that costs everyone an hour. `TEST_CASE` registers a test by placing a constructor in a linker section, but nothing in the application **references** those symbols. The linker, doing exactly its job, discards the entire object file as unused, and the runner finds zero tests. There is no error and no warning: the build succeeds and the menu is blank.

The fix is `WHOLE_ARCHIVE` on the test component in `CMakeLists.txt`, which forces every object in the archive to be linked whether it is referenced or not.

This is a genuinely useful thing to have hit, because it is a concrete encounter with linker garbage collection and section-based registration, which is the same mechanism behind constructor attributes and driver registration tables in larger codebases. "The linker removed my code because nothing referenced it" is a real answer to a real class of embedded bug.

**Console reads did not work over the native USB port.**
Unity's interactive menu needs character-level reads that go through the ROM console path, and the native USB-Serial-JTAG port does not provide them the way the runner expects. Switching to the **UART bridge port** made the menu interactive. Knowing there are two independent console paths on this board (established back in Lab 3) is what made this a five-minute problem instead of an afternoon.

**Task watchdog complaints during the idle menu.**
Expected, not a bug. While the runner sits waiting for input, the idle task on the other core does not get fed within the TWDT window and it says so. Noise, but it looks alarming the first time and it is worth knowing which warnings to ignore, which is its own skill.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM4 flash monitor    # UART bridge port for the interactive menu
```

Ensure `WHOLE_ARCHIVE` is set on the test component or the menu will be empty.

---

## References

- ESP-IDF Programming Guide v5.5, *Unit Testing in ESP32* and *Build System* (`WHOLE_ARCHIVE`)
- Unity assertion reference (ThrowTheSwitch)
