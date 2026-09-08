# Lab 13: Wi-Fi Station and JSON Telemetry

Getting data off the device. Event-driven Wi-Fi bring-up, an HTTP client posting JSON sensor readings to a host, and a retry policy that survives the connection dropping.

| | |
|---|---|
| **Target** | ESP32-S3 N16R8 |
| **Subsystems** | Wi-Fi station mode, LwIP, `esp_http_client`, cJSON |
| **Key APIs** | `esp_wifi_init()`, `esp_event_handler_instance_register()`, `esp_wifi_connect()`, `esp_http_client_perform()`, `cJSON_CreateObject()` |
| **Host** | Python receiver accepting POST |

---

## Objective

Close the loop on the sensor-node architecture from Lab 7: acquire, process, and now **communicate**. Sample the IMU, package the features as JSON, POST them to a listener on the LAN, and handle the network being unavailable without hanging or crashing.

---

## How it works

### Wi-Fi is event-driven, not a blocking call

There is no `wifi_connect_and_wait()`. Bring-up is a sequence of registrations and a state machine driven by callbacks:

1. Initialize NVS (Wi-Fi calibration data is stored there, which is why Lab 10 comes first).
2. Initialize the TCP/IP stack and the default event loop.
3. Register handlers for `WIFI_EVENT` and `IP_EVENT`.
4. Configure the station with SSID and password, start the driver, call `esp_wifi_connect()`.
5. `WIFI_EVENT_STA_START` fires. Later `IP_EVENT_STA_GOT_IP` fires with the assigned address.

**Association and having an IP address are different events**, and this is the trap. `WIFI_EVENT_STA_CONNECTED` means the radio associated with the access point. DHCP has not finished. Any socket opened at that moment fails. The event to wait on is `IP_EVENT_STA_GOT_IP`.

The standard synchronization is an event group: the application task blocks on a bit that the IP event handler sets. That is the correct use of a FreeRTOS primitive as a signal, the same pattern as Lab 7's semaphores.

### Retry and backoff

`WIFI_EVENT_STA_DISCONNECTED` fires when the link drops, and the handler has to decide what to do. Immediately calling `esp_wifi_connect()` in a tight loop against an access point that is down produces a hot loop hammering the radio. A bounded retry count with a growing delay is the minimum, and the reason to implement it deliberately is that "what does your device do when the network goes away" is a real question about real products.

### JSON and the HTTP client

cJSON builds the object in the heap and serializes to a string. Two disciplines matter: **free the string and the object** (this is the easiest heap leak in the whole track, and it recurs every transmission, so it fails in hours rather than immediately), and set `Content-Type: application/json` explicitly, since a server is entitled to reject a body whose declared type does not match.

---

## TM4C123 bridge

The ECE 425 final bolted an HM-10 Bluetooth module onto the TM4C over UART. From the microcontroller's point of view the network was a serial port, and the module handled everything above the byte stream. AT commands in, bytes out.

Here the entire stack is on-chip: MAC, LwIP, DHCP client, TCP, HTTP. That means the application deals with association state, IP acquisition, DNS, and socket lifetime directly. It is more code and considerably more understanding, and it is the difference between "used a Bluetooth module" and "brought up a network stack."

---

## Verification

| Acceptance criterion | How it was confirmed |
|---|---|
| Station associates | `WIFI_EVENT_STA_CONNECTED` logged with the AP's BSSID |
| DHCP completes | `IP_EVENT_STA_GOT_IP` logged with the assigned address, reachable by ping |
| POST succeeds | Device logs HTTP 200, host prints the received JSON body |
| Payload is well formed | Received JSON parses on the host with the expected keys and plausible sensor values |
| Disconnect is survivable | Powering off the AP produces retries with backoff and no crash, and reconnection resumes posting |
| No heap leak | Free heap stable across hundreds of transmissions |

---

## What broke

**The board logged `POST -> HTTP 501` and the laptop printed nothing.**

The receiver was Python's one-line web server, `python -m http.server`. It is built on `SimpleHTTPRequestHandler`, which implements **only `do_GET` and `do_HEAD`**. A POST hits the base class, which correctly answers 501 Not Implemented.

The firmware was right. The host instructions were wrong.

This is worth recording because of the shape of the mistake rather than the fix. The error came back from the network, so the instinct was to suspect the client, the headers, the URL, the content type. Every one of those investigations was a dead end, because the failing component was the thing I had assumed was infrastructure. Replaced with a purpose-built `BaseHTTPRequestHandler` subclass implementing `do_POST` that reads `Content-Length` and prints the body, and it worked on the first try.

The generalizable version: when the error is being reported *by* the component you did not write, check that the component does what you think before debugging the one you did write.

**Sockets opened before DHCP finished.**
Connecting on `STA_CONNECTED` instead of `GOT_IP`. Presents as an intermittent failure that goes away when you add a debug log, because the log takes long enough for DHCP to complete. Classic race, classic Heisenbug.

**Wi-Fi credentials nearly committed to git.**
Caught before pushing. SSID and password now live outside version control, and this is a good reason the repo's `.gitignore` gets attention rather than being an afterthought.

---

## Build

```powershell
idf.py set-target esp32s3
idf.py menuconfig    # set SSID and password, or supply them out of band
idf.py build
idf.py -p COM3 flash monitor
```

Run the host receiver first, and set the host IP in the firmware to the machine actually listening.

---

## References

- ESP-IDF Programming Guide v5.5, *Wi-Fi Driver*, *Event Handling*, *ESP HTTP Client*
- Python `http.server` documentation, `BaseHTTPRequestHandler`
