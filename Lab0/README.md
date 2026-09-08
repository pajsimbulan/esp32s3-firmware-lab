# Lab 0: Toolchain and First Flash

Standing up a complete ESP-IDF development environment from nothing, then proving the whole pipeline (build, flash, monitor) works before a single wire is connected.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8 (Lonely Binary), 16 MB flash, 8 MB octal PSRAM |
| **Framework** | ESP-IDF v5.5.5, `xtensa-esp32s3-elf` GCC, CMake |
| **Peripherals** | None. Board only |
| **Key APIs** | `app_main()`, `esp_chip_info()`, `esp_flash_get_size()`, `vTaskDelay()` |

---

## Objective

Install the toolchain, build the `hello_world` template, flash it over USB, and read the boot log back over serial. The deliverable is not the program, it is a verified build-flash-monitor loop and a documented understanding of what each stage does.

There is a real reason to treat this as a lab rather than a setup step. Every subsequent lab in this repository fails in a way that looks like a code bug when it is actually a toolchain or configuration problem. Knowing exactly what a clean build, a clean flash, and a clean boot look like is what makes those later failures diagnosable instead of mysterious.

---

## What the firmware does

`app_main()` is the application entry point that the ESP-IDF startup code calls after the second-stage bootloader has initialized the clocks, configured the flash controller, mapped the PSRAM, and started the FreeRTOS scheduler. It is not `main()`, and it is not the first code to run.

The program queries the chip at runtime with `esp_chip_info()` and `esp_flash_get_size()`, prints the core count, silicon revision, and feature flags, then counts down and restarts. Reading the chip's own report of its configuration is the fastest possible confirmation that the toolchain, the target setting, and the physical part all agree.

---

## The build system

An ESP-IDF project is a CMake project with a specific structure:

- The **top-level `CMakeLists.txt`** pulls in the IDF build system and names the project.
- **`main/`** is itself a component, registered through `idf_component_register()`.
- **`sdkconfig`** is generated Kconfig output holding several thousand build-time options: FreeRTOS tick rate, log verbosity, flash size, PSRAM mode, optimization level.
- **`sdkconfig.defaults`** is the committed seed for that file. It is the one that belongs in version control.
- **`build/`** holds the ELF, the binary images, the partition table, and `compile_commands.json`, which is what clangd reads for accurate code navigation.

The flash step writes three separate images to three separate offsets: the second-stage bootloader, the partition table, and the application. Understanding that a flash operation is three writes at three addresses, not one file transfer, is what makes partition-table and OTA work in later labs comprehensible rather than magic.

---

## TM4C123 bridge

On the TM4C123 the flow was Keil uVision: a vendor IDE, a vendor project file, a linker scatter file, and a ULINK or ICDI probe writing directly to flash. Startup was a startup assembly file with a vector table, and `main()` really was the first thing to run.

Here the flow is command-line and open: CMake generates Ninja files, Ninja drives GCC, `esptool.py` handles the serial protocol. The startup path is longer and does far more (clock tree, cache configuration, PSRAM mapping, RTOS start) before it reaches application code. The gain is that the entire configuration surface is text, diffable, and reproducible. The cost is a build system with real complexity, which is exactly why this is Lab 0 and not a footnote.

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| Toolchain installed correctly | `idf.py --version` reports v5.5.5; ESP-IDF Doctor reports a clean environment |
| Correct target selected | Build output names `esp32s3`; chip info at runtime reports 2 cores |
| Flash succeeds | `esptool.py` reports hash verification on all three images |
| Application runs | Serial monitor shows the boot log, the chip report, the countdown, and a clean restart in a loop |

---

## What broke

**EIM crashed mid-install even with the IDF path set correctly.**
The ESP-IDF Installation Manager defaults its *tool* download and install paths to `C:\Espressif\` independently of the SDK path. On a machine where that drive is nearly full, the install fails partway through with an error that points at the SDK rather than the tools. Fix: enable "use custom tool download/install folder locations (advanced)" and route all three paths (SDK, dist, tools) explicitly.

**The board did not enumerate as a COM port.**
Two causes, both worth knowing. First, the board has two USB-C ports: the native USB-Serial-JTAG and a separate UART bridge, and they behave differently. Second, a charge-only cable presents no data lines and produces exactly the same symptom as a driver problem. When flashing still fails after that, holding BOOT while tapping RESET forces the ROM download mode manually.

**A new project built for the wrong chip.**
Copying a project directory carries `sdkconfig` with it, and `IDF_TARGET` is cached there. `idf.py set-target esp32s3` is required per project, and it regenerates `sdkconfig` from `sdkconfig.defaults` when doing so. A related trap: `set-target` reconfigures but does not compile, so the `.elf` is absent immediately afterward and any tool expecting it fails with a confusing error. Build first.

**The VS Code extension kept losing its configuration.**
Extension setup is workspace-scoped, not global, so every new project needs the ESP-IDF version, target, and port reselected. After a few stale-flash incidents (firmware running that did not match the source on screen) I moved to driving builds from the command line with `idf.py -p COM3 build flash monitor` and kept the extension for browsing only.

**Flash size reported 2 MB on a 16 MB part.**
Harmless in Lab 0, and a silent time sink in Lab 15. Noted here, fixed in `sdkconfig.defaults` from the start.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor    # Ctrl+] exits the monitor
```

If behavior does not match the source, `idf.py fullclean` and rebuild before debugging anything else.

---

## References

- ESP-IDF Programming Guide v5.5, *Build System* and *Establish Serial Connection*
- ESP32-S3 Technical Reference Manual, Chapter 3 (Boot Mode Control)
- ESP32-S3-WROOM-1 module datasheet
