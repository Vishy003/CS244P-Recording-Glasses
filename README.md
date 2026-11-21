# Recording Glasses — CS244P Group 18

Hands-free camera prototype using a Seeed XIAO ESP32S3 Sense, an external
motion sensor, and a local MQTT/InfluxDB/Grafana server.

## Current implementation

- Verified on hardware: real IMU acceleration/gyroscope readings over USB.
  The tested module identifies as MPU-6500 although its breakout is labeled MPU-6050.
- Verified on hardware: external LED on D1 with a 330 ohm series resistor.
- Verified on hardware: camera initialization, five RAM-only JPEG frames,
  and camera shutdown. Images were discarded; no saved video exists yet.
- Server services and authenticated TLS ingestion were tested using explicitly
  labeled synthetic messages, removed after verification.
- Real sensor delivery over Wi-Fi to Grafana is **not yet verified**.
- Button-controlled RAM-only capture is implemented and pending hardware testing.
  Video storage, remote commands, and reliability testing remain in progress.
  Firmware variants below are separate bench tests.

## Safety and consent

Record only consenting team members and stop if anyone else enters the frame.
Use a team member's private room facing a fixed desk or wall; no classroom,
campus, or public recording. Keep the microphone uninitialized. The external red
LED must turn on before camera capture starts and remain on until capture stops
and any recording file closes. Store project media only on SD/access-controlled
project storage and delete it after final grading. The current camera bench test
uses RAM only, initializes no microphone, enables the LED before camera startup,
and disables it only after successful shutdown. These tests do not implement
saved video, scheduled recording, or remote recording.

## Repository contents

| Path | Purpose |
| --- | --- |
| `firmware-xiao/` | Actual project board: IMU, LED, and camera test variants |
| `firmware/` | Earlier course-kit ESP32 connectivity experiment |
| `compose.yaml`, `config/` | Mosquitto TLS, Telegraf, InfluxDB, and Grafana |
| `scripts/` | Local setup, verification, private configuration, and USB monitor |

## Wiring

| Component | XIAO connection |
| --- | --- |
| IMU VCC / GND | 3V3 / GND |
| IMU SDA / SCL | D4 (GPIO5) / D5 (GPIO6) |
| External red LED | D1 (GPIO2) through 330 ohm resistor to LED anode; cathode to GND |
| Button | D2 (GPIO3) to GND, internal pull-up; based on original project layout |
| Camera | Sense expansion board's camera connector |

Disconnect power before changing wiring. Do not use D8/D9/D10 for the IMU if
using the Sense board's microSD interface.

## Build and run firmware

Install PlatformIO and use a USB data cable. Close other serial monitors before
uploading. Select **one** environment explicitly:

```sh
pio run -d firmware-xiao -e xiao_esp32s3
pio run -d firmware-xiao -e xiao_led_test
pio run -d firmware-xiao -e xiao_camera_test
pio run -d firmware-xiao -e xiao_button_capture
```

Add `-t upload --upload-port <your-port>` to upload an environment, then:

```sh
pio device monitor --port <your-port> --baud 115200
```

`xiao_esp32s3` reads the IMU; it uses USB only without private network settings.
`xiao_led_test` holds D1 HIGH. `xiao_camera_test` automatically captures five
RAM-only frames at startup, stops the camera, and reports the result repeatedly.
Only run the camera environment in the approved private testing setting.
No image bytes are sent through the serial monitor.

## Start the local server

Prerequisites: Docker Desktop, Docker Compose, Python 3, and OpenSSL.

```sh
python3 scripts/setup.py
python3 scripts/verify.py
docker compose ps
```

Grafana: http://localhost:3000/d/group18-vertical-slice
InfluxDB: http://localhost:8086
Username: `admin`; generated passwords are in the ignored local `.env` file.
Grafana/InfluxDB bind to localhost. MQTT defaults to localhost until deliberately
bound to a specific private interface. Verification uses synthetic test data and
is not evidence that the hardware dashboard path works.

For Wi-Fi, put the selected private network's details in `.local/wifi.json`,
then run:

```sh
python3 scripts/enable_private_lan.py <laptop-private-IP>
python3 scripts/prepare_firmware.py --xiao
pio run -d firmware-xiao -e xiao_esp32s3 -t upload --upload-port <your-port>
```

Both devices must be mutually reachable. Firmware validates the MQTT server
certificate and requires network time for TLS. MQTT uses port 8883. Device
telemetry topic: `grp18/devices/group18-glasses-01/telemetry`.
The real-IMU dashboard filters `source=xiao_imu`. The bench firmware uses QoS 0
and does not implement persistent buffering or delivery acknowledgements.

## Button-controlled camera capture (storage pending)

Environment: `xiao_button_capture`. Use only in the approved private room facing
a fixed desk or wall with no non-consenting people in frame.

1. On boot the camera and D1 LED are off; a button held at boot cannot start it.
2. Release the D2-to-GND button, then press once to start capture. The red LED
   turns on before camera initialization. JPEG frames are acquired into RAM and
   discarded; this is not a saved video recording.
3. Press again to stop. The camera shuts down before the LED goes off.
4. Serial output reports state, button level, frame count, and failures. No image
   bytes are printed. The microphone is never initialized, and SD/Wi-Fi are unused.

The button is debounced for 40 ms; capture requests are spaced by at least
100 ms (not a guaranteed frame rate). Frame acquisition failure stops capture.
If camera startup/shutdown fails, the LED remains on and restart is locked out;
disconnect power and investigate. Software commands cannot verify physical LED
illumination. Hardware button/capture/stop behavior has not yet been tested.

To build/upload:

```sh
pio run -d firmware-xiao -e xiao_button_capture -t upload --upload-port <your-port>
pio device monitor --port <your-port> --baud 115200
```

Saving JPEG/MJPEG/video and closing the recording file are future work. When
storage is added, the LED must also remain on until the recording file closes.
