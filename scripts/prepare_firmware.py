#!/usr/bin/env python3
"""Generate an ignored C++ config from private local input; never print secrets."""
import json
import sys
from pathlib import Path
from setup import ROOT, load_env

path = ROOT / '.local' / 'wifi.json'
if not path.exists():
    path.write_text(json.dumps({'ssid': '', 'password': '', 'mqtt_host': ''}, indent=2)+'\n')
    path.chmod(0o600)
wifi = json.loads(path.read_text())
env = load_env()
if not wifi.get('mqtt_host'):
    address = env.get('MQTT_BIND_ADDRESS', '')
    if address and address != '127.0.0.1':
        wifi['mqtt_host'] = address
        path.write_text(json.dumps(wifi, indent=2)+'\n')
        path.chmod(0o600)
if not all(wifi.get(key) for key in ['ssid', 'password', 'mqtt_host']):
    raise SystemExit('Wi-Fi SSID, password, and reachable MQTT host are required. No header generated.')
ca = (ROOT / '.local' / 'mosquitto' / 'ca.crt').read_text()
project = 'firmware-xiao' if '--xiao' in sys.argv[1:] else 'firmware'
output = ROOT / project / 'include' / 'device_secrets.h'
output.write_text('#pragma once\n' + '\n'.join(
    'static const char '+key+'[] = '+json.dumps(value)+';'
    for key, value in [('WIFI_SSID',wifi['ssid']), ('WIFI_PASSWORD',wifi['password']),
        ('MQTT_HOST',wifi['mqtt_host']), ('MQTT_PASSWORD',env['MQTT_DEVICE_PASSWORD']),
        ('MQTT_CA',ca)])+'\n')
output.chmod(0o600)
print('Private firmware config generated; SSID, password, and MQTT host are present.')
print('Wi-Fi credentials stay in .local/wifi.json and the ignored header; never commit either.')
