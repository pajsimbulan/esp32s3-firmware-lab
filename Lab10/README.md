# Lab 10: Non-Volatile Storage

Persisting state across power cycles: a log-structured key/value store built on flash sectors, namespaces, wear levelling, and the partition table that carves the flash up in the first place.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8, 16 MB flash |
| **Subsystem** | NVS (Non-Volatile Storage) over the `nvs` data partition |
| **Demonstrator** | A boot counter that survives reset and power loss |
| **Key APIs** | `nvs_flash_init()`, `nvs_open()`, `nvs_get_u32()`, `nvs_set_u32()`, `nvs_commit()`, `nvs_close()` |

---

## Objective

Store something and get it back after a power cycle. Simple on the surface, and the lab underneath it is really about flash: why you cannot treat it like RAM, what the partition table is, and what happens when a sector fills up.

---

## How it works

### Flash is not RAM and NVS is not a filesystem

NOR flash erases in **sectors** (4 KB here) and writes in smaller units, and a bit can only be programmed from 1 to 0. Changing a stored value in place therefore means erasing an entire 4 KB sector and rewriting it. Sectors also have a finite erase endurance, on the order of 100,000 cycles.

NVS deals with this by being **log-structured**. Writing a key appends a new entry and marks the previous one obsolete rather than modifying it. Reads return the newest live entry for a key. When a sector fills, live entries are compacted into a fresh sector and the old one is erased. That is wear levelling by construction: writes spread across the partition rather than hammering one location.

The practical consequence, and the one worth being able to state: for configuration data written occasionally, wear is not a concern. For anything written at a high rate, NVS is the wrong tool and you want a dedicated data partition with your own scheme.

### Namespaces

`nvs_open("storage", mode, &handle)` opens a namespace, which is a keyspace scoped to a name so two components can both use the key `count` without colliding. Namespaces are created lazily on first write, which matters more than it sounds like it should (see below).

### The partition table

NVS lives in a partition declared in `partitions.csv`, alongside the bootloader, the app, and (from Lab 15) a second app slot and an OTA data partition. Flash is not an undifferentiated blob; it is a table of typed regions with offsets and sizes, and this is the lab where that becomes concrete rather than a diagram.

### `nvs_flash_init()` and its two recoverable errors

The standard idiom, and it is standard for a reason:

```c
esp_err_t err = nvs_flash_init();
if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
}
ESP_ERROR_CHECK(err);
```

`NO_FREE_PAGES` means the partition is full with no room to compact. `NEW_VERSION_FOUND` means the partition was written by a newer NVS format than this build understands. Both are recoverable by erasing, and both are silently fatal if you only check for `ESP_OK`.

### `nvs_commit()`

Writes are buffered until committed. Skipping the commit is the same class of bug as skipping `ledc_update_duty()` in Lab 6: the write succeeds, the value does not persist, and nothing reports an error.

---

## TM4C123 bridge

The TM4C123 had an internal EEPROM block with a word-addressed API (`EEPROMRead` / `EEPROMProgram`), which handled the erase-before-write problem inside the peripheral. There was no namespace concept, no key/value abstraction, and no partition table: you addressed words directly and it was your job to remember what lived where.

NVS adds the key/value layer, the namespace scoping, and the log-structured wear levelling on top of raw flash. The idea that carries over is that non-volatile memory has erase granularity and finite endurance and cannot be written like RAM.

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| Value persists across reset | Boot counter increments by one on each reset rather than restarting at zero |
| Value persists across power loss | Counter continues correctly after the board is unplugged and reconnected |
| Erase behaves as expected | `idf.py erase-flash` returns the counter to zero |
| Partition present and sized | `idf.py partition-table` shows the `nvs` partition at the expected offset |

---

## What broke

**`nvs_open` failed with `ESP_ERR_NVS_NOT_FOUND` on a fresh device.**
The namespace was being opened `NVS_READONLY` first, to read the counter before writing it. But a namespace does not exist until something has been written to it, and a read-only open cannot create one. On a device that had never run the write path, the read failed and the error looked like a corrupted partition.

Two ways out: open `NVS_READWRITE` (which creates the namespace on demand), or treat `ESP_ERR_NVS_NOT_FOUND` as the legitimate first-boot case and default the value. The second is better, because "the key does not exist yet" is normal state on a factory-fresh device, not an error. This exact pattern reappears in Lab 15, where the OTA probation counter has no prior value on first boot.

**A committed value that had not been committed.**
`nvs_set_u32` returned `ESP_OK` and the value did not survive a reset. Missing `nvs_commit()`. Return codes tell you the API accepted the call, not that the effect you wanted has happened.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

Reset the board a few times, then power-cycle it, and watch the counter.

---

## References

- ESP-IDF Programming Guide v5.5, *Non-Volatile Storage Library* and *Partition Tables*
- ESP32-S3 Technical Reference Manual, Chapter on SPI flash and cache
