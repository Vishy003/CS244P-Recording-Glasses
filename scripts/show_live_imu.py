"""Display real XIAO serial sensor readings for the bench demo."""
import serial
from serial.tools import list_ports
ports = [p.device for p in list_ports.comports() if p.vid == 0x303A]
if len(ports) != 1:
    raise SystemExit('Connect one XIAO with a USB data cable, then run again.')
print('LIVE XIAO IMU — USB serial sensor test (not Wi-Fi/dashboard)')
print('Move the sensor gently to change the readings. Ctrl+C stops.\n', flush=True)
try:
    with serial.Serial(ports[0], 115200, timeout=1) as device:
        while True:
            line = device.readline().decode(errors='replace').strip()
            if line:
                print(line, flush=True)
except serial.SerialException as error:
    print('\nUSB serial connection lost or another monitor is using the port.')
    print('Stop other monitors with Ctrl+C, reconnect USB if needed, then run again.')
except KeyboardInterrupt:
    print('\nLive sensor monitor stopped.')
