#!/usr/bin/env python3
"""Smoke-test real services with an explicitly synthetic record; delete it afterward."""
import base64
import csv
from datetime import datetime, timezone, timedelta
import io
import json
import subprocess
import time
import urllib.request
from setup import ROOT, load_env, api
import os

os.chdir(ROOT)
env = load_env()


def request(url, auth=None, payload=None):
    headers = {}
    if auth:
        headers['Authorization'] = 'Basic ' + base64.b64encode(auth.encode()).decode()
    data = None
    if payload is not None:
        headers['Content-Type'] = 'application/json'
        data = json.dumps(payload).encode()
    with urllib.request.urlopen(urllib.request.Request(url, data=data, headers=headers), timeout=15) as r:
        return json.loads(r.read())


def mqtt(client, username=None, password=None, extra=None):
    cmd = ['docker', 'compose', 'exec', '-T', 'mosquitto', client,
           '-h', 'mosquitto', '-p', '8883', '--cafile', '/mosquitto/config/ca.crt', '-V', 'mqttv5']
    if username:
        cmd += ['-u', username, '-P', password]
    return subprocess.run(cmd + (extra or []), capture_output=True, text=True, timeout=15)


auth = 'admin:' + env['GRAFANA_ADMIN_PASSWORD']
for _ in range(45):
    try:
        assert request('http://127.0.0.1:3000/api/health')['database'] == 'ok'
        break
    except Exception:
        time.sleep(2)
else:
    raise RuntimeError('Grafana did not become ready')
assert request('http://127.0.0.1:8086/health')['status'] == 'pass'
print('PASS: InfluxDB and Grafana health endpoints')
ds = request('http://127.0.0.1:3000/api/datasources/uid/group18-influx/health', auth)
assert ds['status'] == 'OK', ds
dashboard = request('http://127.0.0.1:3000/api/dashboards/uid/group18-glasses', auth)
assert dashboard['dashboard']['title'] == 'Group 18 Recording Glasses'
print('PASS: Grafana dashboard provisioned and read-only InfluxDB connection healthy')

anonymous = mqtt('mosquitto_pub', extra=['-t', 'grp18/devices/group18-glasses-01/telemetry', '-m', '{}', '-q', '1'])
assert anonymous.returncode != 0, 'Anonymous MQTT unexpectedly accepted'
print('PASS: anonymous MQTT connection rejected')

denied = mqtt('mosquitto_pub', 'group18-glasses-01', env['MQTT_DEVICE_PASSWORD'],
    ['-t', 'grp18/devices/group18-glasses-01/commands', '-m', '{}', '-q', '1'])
assert denied.returncode != 0 or 'Not authorized' in denied.stderr or 'Not authorized' in denied.stdout, \
    'Device unexpectedly allowed to publish commands'
print('PASS: device account cannot publish recording commands')

now = datetime.now(timezone.utc)
start = (now-timedelta(minutes=2)).isoformat().replace('+00:00','Z')
stop = (now+timedelta(minutes=5)).isoformat().replace('+00:00','Z')
sample = {'device_id': 'group18-glasses-01', 'boot_id': 'synthetic-smoke-test', 'sequence': 1,
          'event': 'heartbeat', 'source': 'synthetic', 'timestamp': now.isoformat(),
          'battery_v': 3.9, 'uptime_s': 1, 'acceleration_m_s2': [0.1,0.2,9.8],
          'angular_velocity_deg_s': [0.0,0.0,0.0], 'potentially_shaky': False,
          'reset_reason': 'synthetic_test'}
pub = mqtt('mosquitto_pub', 'group18-glasses-01', env['MQTT_DEVICE_PASSWORD'],
    ['-t', 'grp18/devices/group18-glasses-01/telemetry', '-m', json.dumps(sample), '-q', '1'])
assert pub.returncode == 0, pub.stderr
query = '''from(bucket: "telemetry") |> range(start: -10m)
|> filter(fn: (r) => r._measurement == "glasses" and r.boot_id == "synthetic-smoke-test")'''
try:
    for _ in range(20):
        req = urllib.request.Request('http://127.0.0.1:8086/api/v2/query?org=group18',
            data=json.dumps({'query': query, 'type': 'flux'}).encode(),
            headers={'Authorization':'Token '+env['INFLUX_READ_TOKEN'],'Content-Type':'application/json','Accept':'text/csv'})
        with urllib.request.urlopen(req, timeout=10) as response:
            result = response.read().decode()
        if 'synthetic-smoke-test' in result and 'battery_v' in result:
            break
        time.sleep(2)
    else:
        raise AssertionError('Synthetic MQTT record did not reach InfluxDB')
    assert 'acceleration_m_s2_0' in result, 'Motion array parsing missing'
    # Exercise the same query endpoint Grafana uses, not just DB connectivity.
    response = request('http://127.0.0.1:3000/api/ds/query', auth, {
        'from': str(int((now-timedelta(minutes=10)).timestamp()*1000)),
        'to': str(int((now+timedelta(minutes=1)).timestamp()*1000)),
        'queries': [{'refId':'A','datasource':{'type':'influxdb','uid':'group18-influx'},
                     'query':query,'intervalMs':10000,'maxDataPoints':100}]})
    assert response['results']['A'].get('frames'), response
    assert not response['results']['A'].get('error'), response
    print('PASS: synthetic JSON -> authenticated MQTT/TLS -> Telegraf -> InfluxDB -> Grafana query')
finally:
    api('/api/v2/delete?org=group18&bucket=telemetry', env,
        {'start':start,'stop':stop,'predicate':'_measurement="glasses" AND boot_id="synthetic-smoke-test"'}, 'POST')
    print('Synthetic test data deleted; no hardware measurements claimed.')

report = {'verified_at_utc': datetime.now(timezone.utc).isoformat(),
          'checks': ['influxdb_health','grafana_health','grafana_datasource','dashboard_provisioning',
                     'mqtt_tls_authenticated_publish','mqtt_anonymous_rejected','mqtt_device_command_acl',
                     'mqtt_to_influxdb','grafana_query','synthetic_data_cleanup'],
          'real_hardware_connected': False,
          'limitations': ['localhost-only deployment','no HTTPS snapshot upload backend yet',
                          'no application storage acknowledgements or deduplication',
                          'no remote-command sender or expiry validation','no alert delivery configured']}
(ROOT/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
print('Verification written to verification.json')
