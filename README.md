# Recording Glasses — CS244P Group 18

Our recording-glasses prototype uses a Seeed XIAO ESP32S3
Sense, an external IMU, a push button, and a red LED. Our local server runs
Mosquitto, Telegraf, InfluxDB, and Grafana on a laptop.

## What we have built and tested

| Component | What our code does | What we have checked |
| --- | --- | --- |
| IMU firmware | Reads acceleration and angular velocity and prints them over USB | We received repeated real readings from the XIAO. Our module identifies as MPU-6500, despite the MPU-6050 label on its breakout. |
| LED test | Drives the external red LED on D1 through a 330 ohm resistor | We confirmed that the physical LED lights up. |
| Camera test | Captures five JPEG frames in RAM, returns the buffers, and stops the camera | Our serial output reported successful initialization, 5/5 frames, and successful shutdown. |
| Button capture | Starts RAM-only camera capture on a D2 button press and stops on the next press; controls the D1 LED | Build verification: PlatformIO compilation passed. |
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
4. Serial output reports state, button level, frame count, and failures. No image
   bytes are printed. The microphone is never initialized, and SD/Wi-Fi are unused.

The button is debounced for 40 ms; capture requests are spaced by at least
100 ms (not a guaranteed frame rate). Frame acquisition failure stops capture.
If camera startup/shutdown fails, the LED remains on and restart is locked out;
disconnect power and investigate. We verified compilation of this firmware with
PlatformIO. The earlier standalone LED test was visually confirmed on the board.

To build/upload:

```sh
pio run -d firmware-xiao -e xiao_button_capture -t upload --upload-port <your-port>
pio device monitor --port <your-port> --baud 115200
```

Each acquired frame is released after counting; this variant captures into RAM.

## USB clock synchronization for local TLS testing

We added an optional USB clock command to the IMU firmware. This lets our laptop
provide the current UTC time when testing MQTT on a hotspot. Certificate
verification stays enabled. The command is accepted over physical USB serial;
we do not accept clock commands over Wi-Fi or MQTT.

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
