# Lab 15: OTA Updates with Rollback

Shipping new firmware to a device in the field and, more importantly, surviving shipping *bad* firmware. Dual application partitions, a probation state machine, an on-device self-test, and automatic rollback when the self-test fails.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8, 16 MB flash |
| **Subsystems** | `esp_https_ota`, `app_update`, partition table, NVS, bootloader rollback |
| **Key APIs** | `esp_https_ota()`, `esp_ota_get_running_partition()`, `esp_ota_get_state_partition()`, `esp_ota_mark_app_valid_cancel_rollback()`, `esp_ota_mark_app_invalid_rollback_and_reboot()` |
| **Config** | `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, `CONFIG_ESPTOOLPY_FLASHSIZE_16MB`, `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP` |

---

## Objective

This is the lab that separates a demo from a product. Anything that has to update itself without a technician touching it needs this, and the interesting half is not downloading the image, it is what happens when the new image is broken.

---

## How it works

### Two app slots and a pointer

The partition table declares `ota_0` and `ota_1`, each large enough for a full application, plus a small `otadata` partition holding which slot the bootloader should boot and what state it is in. An update writes into the slot that is **not** running, so a failure mid-download leaves the working image completely untouched. The last step flips the pointer in `otadata`.

This is why the 2 MB flash default matters so much. Two app slots plus NVS plus the bootloader does not fit in 2 MB on a 16 MB part, and the failures look like flash corruption rather than like a configuration problem.

### The probation state machine

Downloading and booting a new image proves it boots. It does not prove it works. Rollback closes that gap:

1. New image is written, `otadata` points at it, device reboots.
2. The bootloader sees the slot marked `PENDING_VERIFY` and boots it **on probation**.
3. The application runs a self-test: bring up I2C, read `WHO_AM_I` from the IMU, confirm the sensor pipeline produces plausible values.
4. Pass, and it calls `esp_ota_mark_app_valid_cancel_rollback()`, which commits the image permanently.
5. Fail, and it calls `esp_ota_mark_app_invalid_rollback_and_reboot()`, or simply crashes or resets, and the bootloader reverts to the previous slot on its own.

Step 5's fallback is the part that matters: an image so broken it panics before reaching its own self-test still gets rolled back, because the decision lives in the bootloader rather than in the application being tested.

A probation counter in NVS makes reset-during-probation survivable too, using exactly the first-boot pattern from Lab 10 where "no value stored yet" is normal rather than an error.

### The self-test is the design problem

`esp_https_ota` is a library call. Deciding what constitutes "this firmware works" for your specific device is engineering, and it is what an interviewer will actually ask about. Here it is a sensor bring-up check, because a build that cannot read the IMU is useless regardless of whether it boots.

---

## TM4C123 bridge

No equivalent. Updating a TM4C meant a JTAG probe and physical access. Every concept here (dual partitions, a bootloader that arbitrates between images, a probation state, an application that judges itself) is new, and all of it exists because the device is somewhere you are not.

---

## Verification

Four cases, and all four have to be recorded for the claim to mean anything:

| Case | Expected |
|---|---|
| Valid image, self-test passes | Update commits, new version persists across reboots |
| Deliberately broken image, self-test fails | Device reverts to the previous slot on reboot |
| Image that panics before self-test | Bootloader rolls back without application involvement |
| Reset during probation | Probation counter exceeded, rollback triggered |

`FW_VERSION` is bumped every build so the serial log makes the running slot unambiguous.

---

## What broke

**Rollback never armed, and nothing said so.**

The update worked. A deliberately broken image installed, booted, failed its self-test, called `esp_ota_mark_app_invalid_rollback_and_reboot()`, rebooted, and came straight back up as the broken image.

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is **off by default**. Without it the bootloader never puts a new image into `PENDING_VERIFY`, so the app is committed the moment it boots. `esp_ota_get_state_partition()` reports it as already valid, and both mark-valid and mark-invalid become no-ops that return success. The entire safety mechanism is absent and every API call reports OK.

This is the highest-value bug in the track. A safety feature that silently does nothing is worse than one that is absent, because the absent one is obvious. Verifying that the failure path *actually fails* is not paranoia, it is the only way to know the mechanism exists.

**`sdkconfig.defaults` edits that were silently ignored.**
Two separate causes, both worth knowing.

First, `sdkconfig.defaults` is **only read when no `sdkconfig` exists**. Adding a line to defaults on a project that has already been configured does nothing at all. Delete `sdkconfig` and reconfigure, and commit `sdkconfig.defaults` while gitignoring `sdkconfig`.

Second, **deprecated Kconfig aliases are silently ignored**. `CONFIG_OTA_ALLOW_HTTP` and `CONFIG_APP_ROLLBACK_ENABLE` are old names; the current ones are `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP` and `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`. A wrong option name produces no warning, because from Kconfig's point of view it is just a line it does not recognize. Verify against the generated `sdkconfig`, not against what you typed.

**Flash operations failing past 2 MB on a 16 MB part.**
Carried forward from Lab 3, where it was a harmless warning, all the way to here, where it is a hard blocker. Documented at every lab it appeared in rather than retroactively cleaned up, because the fact that it sat visible and ignored for twelve labs is itself the lesson.

**Board stopped enumerating on the second dev machine.**
COM port enumeration failure on the laptop mid-lab, unrelated to the firmware. Worth noting only because the temptation when the device vanishes is to assume the OTA bricked it.

---

## Build

```powershell
Remove-Item sdkconfig -ErrorAction SilentlyContinue
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

Confirm in the generated `sdkconfig` that `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` before believing any rollback result. Serve the image from a host reachable at `HOST_IP` and bump `FW_VERSION` every build.

---

## References

- ESP-IDF Programming Guide v5.5, *Over The Air Updates (OTA)*, *App Image Format*, *Partition Tables*, *Bootloader*
