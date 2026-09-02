# Lab 15 Phase 1 — Complete Code Walkthrough

Companion to `main.c`. Every function in that file is walked here. Nothing is left as
"bring this from an earlier lab."

Read order: this document top to bottom matches the file top to bottom, which matches C's
declare-before-use requirement.

---

## Block 0 · Includes and configuration

```c
#include <inttypes.h>
#include "driver/i2c_master.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "protocol_examples_common.h"
```

**`esp_https_ota.h` vs `esp_ota_ops.h`** — two layers, and keeping them straight matters.

| Header | Layer | Job |
|---|---|---|
| `esp_https_ota.h` | transport | download an image, write it to the inactive slot |
| `esp_ota_ops.h` | state machine | which slot am I on, what state is it in, mark valid/invalid |

They are independent. A device that never downloads anything still calls
`esp_ota_get_running_partition()` on boot. Lab 15 uses both.

**`driver/i2c_master.h`, not `driver/i2c.h`.** ESP-IDF v5 introduced a new I2C driver API.
The old one still compiles but emits deprecation warnings, and mixing the two in one project
fails at the bus level. Your earlier I2C lab may have used the legacy API; this file uses
the current one.

**`<inttypes.h>`** provides `PRIx32` and `PRIu32`. ESP-IDF v5 warns if you print a
`uint32_t` with `%x`, because the width of `int` is not guaranteed to match. These macros
expand to the correct specifier for the platform.

### The config block

```c
#define HOST_IP        "192.168.1.50"
#define FIRMWARE_URL   "http://" HOST_IP ":" HOST_PORT "/lab15_ota.bin"
```

Adjacent string literals concatenate at compile time in C, so those fragments become one
string. Change `HOST_IP` in one place and both URLs follow.

```c
#define FW_VERSION     "v1-baseline"
```

**Bump this every build.** It is how you tell from the serial log which image is running.
In proof cases 3 and 4 you will have three builds in play — good, self-test-fails, and
crash-on-boot — and without a version string you cannot tell them apart.

> **[TM4C BRIDGE]**
>
> On the TM4C there was exactly one binary and you knew what it was because you had just
> flashed it. Here the device chooses between two images on its own. The version string is
> how you regain the certainty you used to get for free.

---

## Block 1 · `i2c_bringup()`

```c
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_mpu_dev = NULL;
```

Two file-scope handles. The `s_` prefix is a common convention for file-static state.

The v5 driver splits what used to be one concept into two: a **bus** (the physical SDA/SCL
pair) and **devices** on that bus (each with its own address and clock speed). That split
is why there are two handles and two config structs.

> **[TM4C BRIDGE]**
>
> On the TM4C you wrote `I2C1_MSA_R = (addr << 1) | 0`, putting the slave address into a
> register for every single transaction. Here the address is bound once into a device handle
> and the driver fills it in. Same operation, moved from your code into the driver.

### Bus configuration

```c
i2c_master_bus_config_t bus_cfg = {
    .i2c_port                     = I2C_PORT,
    .sda_io_num                   = I2C_SDA_IO,
    .scl_io_num                   = I2C_SCL_IO,
    .clk_source                   = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt            = 7,
    .flags.enable_internal_pullup = true,
};
```

**`.sda_io_num` / `.scl_io_num`** — the ESP32-S3 has a GPIO matrix, so almost any pin can
be I2C. That flexibility is why you must say which. On the TM4C, I2C1 was on PA6/PA7 and
that was that.

**`.glitch_ignore_cnt = 7`** — the hardware filters pulses shorter than 7 source-clock
cycles. Long wires and breadboards pick up noise that can look like a clock edge. This is a
hardware debounce for the bus, and 7 is Espressif's recommended default.

**`.flags.enable_internal_pullup = true`** — I2C is open-drain: devices pull the line low
and a resistor pulls it high. Without pull-ups the bus floats and nothing works. The
internal ones are weak (tens of kΩ), fine for short breadboard runs at 400 kHz. If your
breakout already has pull-ups fitted, both sets in parallel is still fine.

### Device configuration

```c
i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address  = MPU_ADDR,          // 0x68
    .scl_speed_hz    = I2C_FREQ_HZ,       // 400 kHz
};
```

**`0x68` is the 7-bit address**, not the shifted 8-bit form. The driver shifts it and adds
the R/W bit. Passing `0xD0` here is a classic error that produces silence on the bus.

**`.scl_speed_hz` is per device**, which is the point of the bus/device split — a slow
sensor and a fast one can share a bus and each gets its own clock rate.

### Error handling

Each call is checked separately and logs its own message. `i2c_new_master_bus` failing means
a pin conflict or a bad port number. `i2c_master_bus_add_device` failing means a bad address
or an exhausted device slot. Different faults, different fixes, so different log lines.

---

## Block 2 · `mpu_read()`

```c
static esp_err_t mpu_read(uint8_t reg, uint8_t *buf, size_t len)
{
    if (s_mpu_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(s_mpu_dev, &reg, 1, buf, len,
                                       pdMS_TO_TICKS(1000));
}
```

**The NULL guard** matters because `self_test()` can in principle run before
`i2c_bringup()` if someone reorders `app_main`. Returning an error beats dereferencing NULL
and panicking.

**`i2c_master_transmit_receive()` is one transaction, not two.** It performs:

```
START | addr+W | reg | RESTART | addr+R | data... | STOP
```

The **repeated START** is the important part. A plain STOP between the write and the read
would release the bus, and another master could interleave a transaction and move the
device's internal register pointer. The repeated START holds the bus across both halves.

> **[TM4C BRIDGE]**
>
> This is exactly the sequence you built by hand on the TM4C with
> `I2C1_MCS_R = I2C_MCS_START | I2C_MCS_RUN` followed by a second START without a STOP.
> Same protocol, one function call.

**The 1000 ms timeout.** I2C at 400 kHz moves a byte in ~20 µs, so a legitimate transaction
finishes in well under a millisecond. A one-second timeout is not generous — it is a
declaration that anything taking this long means the bus is **stuck**, usually a slave
holding SDA low. The timeout is what stops that from hanging your boot forever.

---

## Block 3 · `health_check_ok()`

```c
esp_err_t err = esp_http_client_perform(client);
int status    = esp_http_client_get_status_code(client);
esp_http_client_cleanup(client);

if (err == ESP_OK && status >= 200 && status < 300) {
```

**This is the most transferable idea in the file.**

`esp_http_client_perform()` returns `ESP_OK` when the **HTTP exchange completed**. It says
nothing about whether the server liked the request. A 404, a 500, a 501 — all are completed
exchanges. The server heard you, understood you, and said no.

Two independent questions:

| Question | Answered by |
|---|---|
| Did the conversation happen? | `err` |
| Did the server agree? | `status` |

Checking only `err` is the Lab 13 bug: a totally failing setup printed a cheerful success
line because 501 arrived successfully.

> **[KEY POINT] Interview-grade detail**
>
> "The client returned OK but the request had failed" is a bug class every network
> programmer meets once. Being able to explain *why* the API separates transport success
> from application success is the difference between having hit it and having understood it.

**`.timeout_ms`** — an unbounded network call in a boot path is a latent hang. No server, no
response, no timeout, and the device sits in `app_main` forever. Bound every network
operation.

**`esp_http_client_cleanup()` before the branch, not inside it.** Every path frees the
handle exactly once. Freeing inside each branch is how handle leaks get written.

**Retries with a fixed 2 s gap.** §15B.4's standard: "three attempts over 60 seconds, then
roll back" is a defensible budget; "check once" is not. The tension is real in both
directions — too few retries rolls back a good image over a server restart, too many delays
a genuine rollback.

**`pdMS_TO_TICKS`** — your tick rate is **100 Hz**, so `vTaskDelay(1)` is 10 ms, not 1 ms.
The macro converts correctly regardless of tick rate. Never pass a raw number.

> **[TM4C BRIDGE]**
>
> `SysTick_Wait1ms()` was a busy-wait spinning the CPU. `vTaskDelay()` **blocks the task**
> and yields to the scheduler. Other tasks run during those two seconds. That is the
> bare-metal / RTOS difference in one line.

---

## Block 4 · Creating the NVS namespace (four lines in `app_main`)

Not in the manual. These lines exist because of a bug the manual's `self_test()` walks
straight into.

```c
nvs_handle_t h;
if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_u32(h, "ok", 1);
    nvs_commit(h);
    nvs_close(h);
}
```

**The problem.** `self_test()` opens the same namespace with `NVS_READONLY`, which returns
`ESP_ERR_NVS_NOT_FOUND` when the namespace does not exist. Nothing in the manual's program
ever *writes* to `"storage"`, so it is never created, the check fails on every probation
boot, and **every OTA image rolls itself back.** You would see the device ping-ponging
between slots and conclude OTA was broken.

**The fix.** `NVS_READWRITE` creates the namespace if absent. Writing one value is enough to
make it exist.

**`nvs_commit()`** is what writes to flash. Without it the change lives in RAM and is lost
on reset.

> **[KEY POINT] The lesson worth keeping**
>
> §15B.4's claim is that the self-test is the part that actually matters. Here is the proof:
> a self-test checking for a resource the application never creates turns a safety mechanism
> into a boot loop. **A self-test can be wrong in the direction of false failure, not just
> false success**, and false failure is what makes a fleet unupdatable.

---

## Block 5 · `self_test()`

### Check 1 — storage

```c
if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
```

Cheapest check, so it goes first. `NVS_READONLY` is load-bearing: read-write would *create*
the namespace, making the check always pass. The mode is doing real work.

`nvs_close(h)` on the success path only — on failure there is no handle to close.

### Check 2 — sensor

```c
esp_err_t err = mpu_read(MPU_WHO_AM_I, &who, 1);
if (err != ESP_OK)                              { /* no ACK */ }
if (who != 0x70 && who != 0x68 && who != 0x71)  { /* wrong part */ }
```

**Two distinct failures, deliberately separated:**

- `mpu_read` returns non-OK → the I2C transaction failed, no ACK. The device is not
  electrically present: bad joint, pulled wire, dead part.
- The read succeeds but the value is wrong → something answered at that address and it is
  not what you expect.

Collapsing these into one check would cost you the diagnostic. At 3 AM the log line is all
you have.

**Your breakout returns 0x70** — MPU6500 silicon in an MPU6050-style package. The accepted
set covers the variants you might encounter.

> **[TM4C BRIDGE]**
>
> Same instinct as reading back a peripheral register after configuring it in ECE 425 to
> confirm the hardware was really there. Now it is a boot-time gate on a safety decision
> rather than a debug aid.

### Check 3a — associated

```c
if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK)
```

Non-OK when the station is not associated with an AP.

**Necessary but not sufficient**, and §15B.4 flags this as a correction to an earlier draft
that treated it as proof of connectivity. Association proves the radio is talking to an
access point. It does not prove DNS resolves, a route exists, or your server is reachable.

### Check 3b — reachable

```c
if (!health_check_ok(HEALTH_URL, 3, 3000))
```

The reason 3a is not enough, and the most important line in the function.

Picture the failure it prevents: an image boots, reads its sensor, associates with Wi-Fi,
marks itself valid, and cannot reach the update server. The device is alive, looks healthy,
and **can never be updated again.** Worse than a brick, because a brick is obvious.

Espressif's own security guidance suggests a successful connection to the OTA server as the
checkpoint before cancelling rollback.

### The structural choices

**Early return per check.** `if (bad) { log; return false; }` throughout. One reason logged
per failure.

Structuring it as `bool ok = true; ok = ok && check1() && check2();` would short-circuit and
skip later checks, so you would learn only the first thing that broke.

**`return true` is reachable only after every check passes.** No default-pass path exists.

---

## Block 6 · `do_ota()`

```c
esp_http_client_config_t http = {
    .url        = url,
    .timeout_ms = 10000,
};
```

The same struct as Lab 13, reused unchanged, because an OTA download **is** an HTTP GET —
one whose response body happens to be a firmware image.

**Designated initializers zero every unmentioned field.** Not incidental here:
`.crt_bundle_attach` is `NULL` because you did not set it, and `NULL` is exactly what tells
the TLS layer to skip certificate verification. **On Path A the omission is the
configuration.**

**The absent cert bundle is the Lab 15 errata.** §15.4's code attaches
`esp_crt_bundle_attach` while §15.3 says the lab uses plain HTTP. Both cannot be true. A
certificate bundle verifies a TLS chain; a plain HTTP server presents no certificate at all.
Attaching the bundle and serving over HTTP fails in a way that reads like a TLS problem and
is actually a configuration contradiction.

**The function is still named `esp_https_ota()` on Path A.** The name reflects intended use,
not enforcement; `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP` is what relaxes the requirement.

### What `esp_https_ota()` does in one call

1. Opens the HTTP connection
2. Reads the image header, validates magic byte and chip target
3. `esp_ota_begin()` on the inactive partition
4. Streams the body into flash in chunks
5. Verifies the SHA-256 checksum
6. `esp_ota_end()` and `esp_ota_set_boot_partition()` — **writes `otadata`**

**Step 6 is the atomic swap.** Nothing is committed until `otadata` changes. Lose power at
step 4 and `otadata` still points at the working slot, so the device boots normally on the
old image. That is the entire A/B guarantee, and it lives in an 8 KB partition.

### The two branches

**Success → `esp_restart()`.** The new image is in flash and `otadata` points at it, but the
CPU is still executing the old one. A reboot is required. Does not return.

**Failure → log and continue.** Deliberate. The inactive slot holds garbage and `otadata`
was never touched, so the running image is fine. Rebooting would be pointless churn.

**`esp_err_to_name(err)`** turns a numeric code into `ESP_ERR_HTTP_CONNECT` or similar.
Cheap, and the difference between a usable log and a mystery.

---

## Block 7 · `ota_trigger_task()`

```c
static void ota_trigger_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(OTA_DELAY_MS));
    do_ota(FIRMWARE_URL);
    vTaskDelete(NULL);
}
```

**Why a task and not a call in `app_main`.** Three reasons:

1. The delay gives you a window to read the boot log and confirm which slot is running
   before the update starts.
2. `app_main` stays free to run the rest of the application.
3. `vTaskDelay` in `app_main` blocks the main task, usually undesirable in a real program.

**`vTaskDelete(NULL)`** — `NULL` means "delete the calling task." Only reached if the OTA
failed, since success reboots. A FreeRTOS task function must never return; falling off the
end causes a panic. Deleting yourself is the correct exit.

```c
xTaskCreate(ota_trigger_task, "ota_trig", 8192, NULL, 5, NULL);
```

**8192 bytes of stack, not the usual 2048.** The HTTP client and mbedTLS allocate large
buffers on the stack. A too-small stack here produces a stack-overflow panic whose backtrace
points somewhere inside mbedTLS and looks unrelated to OTA. This is the kind of number you
tune by measuring with `uxTaskGetStackHighWaterMark()` rather than guessing.

> **[TM4C BRIDGE]**
>
> Bare metal had one stack, sized by the linker script, shared by everything. Every FreeRTOS
> task gets its own, and **you** pick the size. Per-task stack sizing is a genuine embedded
> skill with no bare-metal equivalent, and a common interview question.

---

## Block 8 · `resolve_probation()`

Factored out of `app_main` so the boot sequence reads clearly.

```c
if (esp_ota_get_state_partition(running, &st) != ESP_OK ||
    st != ESP_OTA_IMG_PENDING_VERIFY) {
    return;
}
```

**Inverted guard clause** rather than a nested `if`. Same logic as §15B.5, less indentation.

**`||` short-circuits**, so `st` is never read if the first call fails. The ordering is
deliberate, exactly as the `&&` version in the manual is.

**This returns immediately on a normal boot.** If the image is already `VALID` there is no
probation to resolve. That is what makes the function safe to call unconditionally.

```c
if (self_test()) {
    ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
} else {
    esp_ota_mark_app_invalid_rollback_and_reboot();   // does not return
}
```

**`esp_ota_mark_app_valid_cancel_rollback()`** — the name is unusually explicit, "mark
valid" *and* "cancel rollback," because it does both. The naming reflects that skipping it
is not a neutral omission.

**`esp_ota_mark_app_invalid_rollback_and_reboot()`** — discards this image and reboots into
the previous slot. **Does not return.** Anything after it is unreachable. Note it is not
wrapped in `ESP_ERROR_CHECK`: there is no useful recovery from a failed rollback.

> **[KEY POINT] The asymmetry is the safety property**
>
> Read the two names side by side. **Validity must be actively claimed. Invalidity is the
> default.**
>
> If neither is reached — the image crashed, hung, or the watchdog fired — the image stays
> `PENDING_VERIFY` and the next boot rolls it back anyway. **The failure path requires no
> cooperation from the broken firmware.** That is what makes it trustworthy, and it is the
> answer when an interviewer asks how you guarantee a bad update cannot brick a fleet.

---

## Block 9 · `app_main()`

### Section 1 — bring up what the self-test depends on

```c
esp_err_t err = nvs_flash_init();
if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
}
ESP_ERROR_CHECK(err);
```

The manual's bare `ESP_ERROR_CHECK(nvs_flash_init())` panics if the NVS partition is full or
was written by a newer NVS version. Both happen in practice — a truncated partition after a
layout change produces exactly this. Erase and retry is the standard recovery and appears in
most ESP-IDF examples.

```c
ESP_ERROR_CHECK(example_connect());
ESP_ERROR_CHECK(i2c_bringup());
nvs_seed();
```

**This ordering is the whole point of the section.**

The self-test checks NVS, the sensor, and the network. All three must be up **before** it
runs. Putting the probation block at the top of `app_main` — the natural instinct, and what
§15.4 explicitly encourages by saying "run this early on every boot" — makes check 3 fail on
every boot and **rolls back a perfectly good image**.

The general principle: a boot-time safety gate is in tension with running early. Resolve it
by bringing up only what the test needs, then testing, then everything else.

**`example_connect()` blocks** until an IP is assigned, which is the property the ordering
relies on.

### Section 2 — announce slot and build

```c
const esp_partition_t *running = esp_ota_get_running_partition();
ESP_LOGI(TAG, "boot: %s running from %s at 0x%08" PRIx32,
         FW_VERSION, running->label, running->address);
```

Returns a pointer to a descriptor **the system owns**. You read it; you do not free it.

`running->label` is the name from your CSV: `ota_0` or `ota_1`. **This line is what makes
proof case 2 demonstrable** — without it you cannot show the slots alternating.

`running->address` prints `0x00020000` or `0x001e0000`, the offsets from your partition
table. The same C source runs from two different flash addresses depending on which slot the
bootloader chose. Seeing both in one log is the clearest evidence A/B is real.

### Section 3 — resolve probation

One call. All the logic is in Block 8.

### Section 4 — normal application

```c
xTaskCreate(ota_trigger_task, "ota_trig", 8192, NULL, 5, NULL);

while (1) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGI(TAG, "alive (%s on %s)", FW_VERSION, running->label);
}
```

The heartbeat is not decoration. In proof case 4 you need to distinguish "the image booted
and then rolled back" from "the image never booted at all." A heartbeat that appears once
and stops tells you which.

`app_main` may return in ESP-IDF — the task is simply deleted — but keeping it alive gives
you the heartbeat.

---

## The state machine, assembled

```
    flashed over USB
          |
          v
     [ VALID ] <-------------------------+
          |                              |
     do_ota() writes the inactive        |  bootloader reverts:
     slot, then otadata                  |  state is still PENDING
          |                              |
          v                              |
     [ PENDING_VERIFY ] ----------------+
          |                        (crash, hang, watchdog,
     self_test() passes             or mark_app_invalid)
          |
          v
     [ VALID ]
```

Four exits from `PENDING_VERIFY`: pass the self-test (→ VALID), fail it (→ rollback), crash
before either (→ rollback), hang (→ watchdog → rollback). **Three of four end in rollback,
and only one requires the firmware to participate.**

---

## Before you build

Two edits in `main.c`:

1. `HOST_IP` → your laptop's IPv4 from `ipconfig`
2. `I2C_SDA_IO` / `I2C_SCL_IO` → your actual wiring

If your IMU is not wired up yet, comment out check 2 in `self_test()` and the
`i2c_bringup()` call. Everything else works without it, and you can add the sensor back
before attempting proof cases 3 and 4.

---

## What to record in `debug_log.md`

Not the code. These four:

1. **The NVS namespace trap** — a self-test checking for a resource the app never created
   would have rolled back every image. False failure, not false success.
2. **The ordering trap** — where you put `resolve_probation()` first, and what happened.
3. **`perform() == ESP_OK` with a non-2xx status**, if `health_check_ok` hits it.
4. **Proof case 4** — an image too broken to participate in its own recovery, rolled back
   anyway.

Item 4 is the interview story. Items 1 and 2 show judgement, because both are failures of a
safety mechanism in the direction of *breaking working things*, which is the harder failure
mode to reason about.