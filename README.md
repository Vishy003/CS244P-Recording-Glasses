# Recording Glasses — CS244P Group 18

Our recording-glasses prototype uses a Seeed XIAO ESP32S3
Sense, an external IMU, a push button, and a red LED. Our local server runs
Mosquitto, Telegraf, InfluxDB, and Grafana on a laptop.

## What we have built and tested

| Component | What our code does | What we have checked |
| --- | --- | --- |
| IMU firmware | Reads acceleration and angular velocity and prints them over USB | We received repeated real readings from the XIAO. Our module identifies as MPU-6500, despite the MPU-6050 label on its breakout. |
| LED test | Drives the external red LED on D1 through a 330 ohm resistor | We confirmed that the physical LED lights up. |
| Camera test | Captures a selected number of JPEG frames in RAM, returns the buffers, and stops the camera | We received 20/20 frames at 320×240 with successful initialization and shutdown. We also visually confirmed the external LED during this test. |
| Button capture | Implements D2 button handling for starting and stopping RAM capture with the D1 LED | Build verification: PlatformIO compilation passed. |
| Local server | Receives authenticated MQTT/TLS messages through Telegraf into InfluxDB and makes them queryable through Grafana | Our server verification passed using labeled synthetic messages, which the verification script deleted afterward. |

We keep the IMU, LED, camera, and button-capture tests in separate firmware
variants. The table above records the checks we have completed for each one.

## Safety and consent

We test only in a team member's private room, facing a fixed desk or wall. We
record only consenting team members and stop if anyone else enters the frame.
We do not record in classrooms, on campus, or in public spaces. Our camera
firmware never initializes the microphone. We turn the red LED on before camera
initialization and keep it on until the camera has stopped.

Our consent plan requires any retained project media to stay on SD or our
access-controlled project server and be deleted after grading. Our current camera tests use RAM only and discard captured frames. On a camera startup or shutdown
error, the code keeps the LED on and reports the error.

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
| External red LED | D1 (GPIO2) through 330 ohm resistor to LED anode. LED cathode connects to GND |
| Button | Firmware uses D2 (GPIO3) with an internal pull-up and expects a switch to GND |
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

`xiao_esp32s3` reads the IMU. It uses USB only without private network settings.
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
Username: `admin`. Generated passwords are in the ignored local `.env` file.
Grafana/InfluxDB bind to localhost. MQTT defaults to localhost until deliberately
bound to a specific private interface. Our server verification checks this path using labeled synthetic test data.

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
The real-IMU dashboard filters `source=xiao_imu`. The device firmware publishes
telemetry using MQTT QoS 0.

## Button-controlled RAM capture

Environment: `xiao_button_capture`. Use only in the approved private room facing
a fixed desk or wall with no non-consenting people in frame.

1. On boot the camera and D1 LED are off; a button held at boot cannot start it.
2. Release the D2-to-GND button, then press once to start capture. The red LED
   turns on before camera initialization. JPEG frames are acquired into RAM and
   returned to the camera driver after each capture.
3. Press again to stop. The camera shuts down before the LED goes off.
4. Serial output reports state, button level, frame count, and failures. The first
   JPEG frame is exported as a USB preview. The microphone is never initialized, and SD/Wi-Fi are unused.

The button is debounced for 40 ms. Capture requests are spaced by at least
100 ms (not a guaranteed frame rate). Frame acquisition failure stops capture.
If camera startup/shutdown fails, the LED remains on and restart is locked out.
Disconnect power and investigate. We verified compilation of this firmware with
PlatformIO. The earlier standalone LED test was visually confirmed on the board.

To build/upload:

```sh
pio run -d firmware-xiao -e xiao_button_capture -t upload --upload-port <your-port>
pio device monitor --port <your-port> --baud 115200
```

Each acquired frame is released after counting. This variant captures into RAM.

## USB clock synchronization for local TLS testing

We added an optional USB clock command to the IMU firmware. This lets our laptop
provide the current UTC time when testing MQTT on a hotspot. Certificate
verification stays enabled. The command is accepted only over physical USB serial.

With the IMU firmware installed and other serial monitors closed:

```sh
python3 -m pip install pyserial
python3 scripts/show_live_imu.py --sync-clock
```

The script sends the laptop's current UTC epoch and displays device diagnostics.
Serial messages report Wi-Fi association, certificate-validated MQTT connection,
and real sensor publications. To check receipt, open the Grafana dashboard and
look for the corresponding readings. The firmware retries Wi-Fi connections,
scans the D4/D5 bus once when the sensor is absent, and tests the reversed D4/D5
assignment during startup. If neither orientation identifies the IMU, it returns
to SDA D4 / SCL D5. Sensor data is published only after a successful hardware read.

The LAN certificate includes both an IP SAN for desktop clients and a numeric
DNS SAN for the Arduino core's mbedTLS hostname verifier.

## Longer camera indicator test

Our camera bench test defaults to five frames. We can select 1–60 frames at
build time using `CAMERA_TEST_FRAMES`. For example, twenty frames spaced about
one second apart give us a longer interval to observe the external D1 LED:

```sh
PLATFORMIO_BUILD_FLAGS='-DCAMERA_TEST_FRAMES=20' pio run -d firmware-xiao -e xiao_camera_test -t upload --upload-port <your-port>
```

We received 20/20 frames at 320×240 and successful camera shutdown in this test.
We visually confirmed that the external red LED lit during capture.
Frames are discarded from RAM. The microphone stays uninitialized. The LED is
commanded on before camera startup and off after successful shutdown. This
firmware runs the finite test again after a board restart.

## Button capture with a USB image preview

We verified a physical button start and stop cycle on November 23. The red LED
was visually confirmed on during capture and off after stopping. The session
acquired 299 frames with zero acquisition failures. We received and opened a
320×240 JPEG preview in VS Code. The camera was idle after the final stop.

The button firmware exports the first frame of each session over USB. Run
`python scripts/show_button_capture.py --port <your-port>` using a Python
environment with pyserial installed. The monitor validates the JPEG length and
markers, then saves it under the ignored `.local/captures/` directory.
The microphone remains uninitialized. This captures frames and a local preview
image rather than a saved video file.
