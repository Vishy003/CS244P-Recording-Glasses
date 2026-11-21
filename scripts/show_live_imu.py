"""Display real XIAO serial sensor readings for the bench demo."""
import serial
import argparse
import time
from serial.tools import list_ports
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sync-clock', action='store_true', help='Set XIAO UTC from this trusted USB laptop for TLS.')
args = parser.parse_args()
ports = [p.device for p in list_ports.comports() if p.vid == 0x303A]
if len(ports) != 1:
    raise SystemExit('Connect one XIAO with a USB data cable, then run again.')
print('LIVE XIAO IMU — serial diagnostics; dashboard delivery must be checked separately')
print('Move the sensor gently to change the readings. Ctrl+C stops.\n', flush=True)
try:
    with serial.Serial(ports[0], 115200, timeout=1) as device:
        if args.sync_clock:
            device.write(('TIME '+str(int(time.time()))+'\n').encode('ascii'))
            device.flush()
        while True:
            line = device.readline().decode(errors='replace').strip()
            if line:
                print(line, flush=True)
except serial.SerialException as error:
    print('\nUSB serial connection lost or another monitor is using the port.')
    print('Stop other monitors with Ctrl+C, reconnect USB if needed, then run again.')
except KeyboardInterrupt:
    print('\nLive sensor monitor stopped.')
