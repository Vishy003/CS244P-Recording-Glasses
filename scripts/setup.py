#!/usr/bin/env python3
"""Create private local credentials/certificates, start services, scope DB tokens."""
import json
import os
from pathlib import Path
import secrets
import subprocess
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)


def run(*args):
    subprocess.run(args, check=True)


def load_env():
    p = ROOT / '.env'
    return dict(line.split('=', 1) for line in p.read_text().splitlines()
                if line and not line.startswith('#')) if p.exists() else {}


def save_env(env):
    p = ROOT / '.env'
    p.write_text(''.join(f'{k}={v}\n' for k, v in env.items()))
    p.chmod(0o600)


def api(path, env, payload=None, method=None):
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request('http://127.0.0.1:8086' + path, data=data,
          headers={'Authorization': 'Token ' + env['INFLUX_ADMIN_TOKEN'],
                   'Content-Type': 'application/json'}, method=method)
    with urllib.request.urlopen(req, timeout=10) as response:
        body = response.read()
        return json.loads(body) if body else {}


def main():
    env = load_env()
    for key in ['INFLUX_ADMIN_PASSWORD', 'INFLUX_ADMIN_TOKEN', 'GRAFANA_ADMIN_PASSWORD',
                'MQTT_DEVICE_PASSWORD', 'MQTT_INGEST_PASSWORD', 'MQTT_OPERATOR_PASSWORD']:
        env.setdefault(key, secrets.token_hex(24))
    env.setdefault('INFLUX_READ_TOKEN', '')
    env.setdefault('INFLUX_WRITE_TOKEN', '')
    save_env(env)
    private = ROOT / '.local'
    private.mkdir(exist_ok=True, mode=0o700)
    private.chmod(0o700)
    ca = private / 'ca'
    ca.mkdir(exist_ok=True, mode=0o700)
    broker = private / 'mosquitto'
    broker.mkdir(exist_ok=True)
    if not (broker / 'server.crt').exists():
        def openssl(*args):
            subprocess.run(['openssl', *args], check=True, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
        openssl('req', '-x509', '-newkey', 'rsa:3072', '-nodes', '-days', '365',
                '-keyout', str(ca/'ca.key'), '-out', str(broker/'ca.crt'),
                '-subj', '/CN=Group18 Local Development CA', '-addext', 'basicConstraints=critical,CA:TRUE')
        openssl('req', '-newkey', 'rsa:3072', '-nodes', '-keyout', str(broker/'server.key'),
                '-out', str(ca/'server.csr'), '-subj', '/CN=mosquitto')
        (ca/'server.ext').write_text('subjectAltName=DNS:localhost,DNS:mosquitto,IP:127.0.0.1\n'
                                    'extendedKeyUsage=serverAuth\nbasicConstraints=CA:FALSE\n')
        openssl('x509', '-req', '-in', str(ca/'server.csr'), '-CA', str(broker/'ca.crt'),
                '-CAkey', str(ca/'ca.key'), '-CAcreateserial', '-out', str(broker/'server.crt'),
                '-days', '365', '-extfile', str(ca/'server.ext'))
        (ca/'ca.key').chmod(0o600)
        # Docker's non-root Mosquitto user must read its mounted key. The parent
        # .local directory is mode 0700 on the host; only the broker gets this mount.
        (broker/'server.key').chmod(0o644)
    (broker/'mosquitto.conf').write_text('''persistence true
persistence_location /mosquitto/data/
log_dest stdout
log_type error
log_type warning
log_type notice
listener 8883
allow_anonymous false
password_file /mosquitto/config/passwords
acl_file /mosquitto/config/acl
cafile /mosquitto/config/ca.crt
certfile /mosquitto/config/server.crt
keyfile /mosquitto/config/server.key
tls_version tlsv1.2
max_connections 25
max_packet_size 262144
''')
    (broker/'acl').write_text('''user group18-glasses-01
topic write grp18/devices/group18-glasses-01/telemetry
topic write grp18/devices/group18-glasses-01/events
topic write grp18/devices/group18-glasses-01/acks
topic read grp18/devices/group18-glasses-01/commands

user ingest
topic read grp18/devices/+/telemetry
topic read grp18/devices/+/events

user operator
topic write grp18/devices/group18-glasses-01/commands
topic read grp18/devices/+/acks
''')
    if not (broker/'passwords').exists():
        (broker/'passwords').write_text(
            'group18-glasses-01:' + env['MQTT_DEVICE_PASSWORD'] + '\n' +
            'ingest:' + env['MQTT_INGEST_PASSWORD'] + '\n' +
            'operator:' + env['MQTT_OPERATOR_PASSWORD'] + '\n')
        # Hash the newly generated passwords before the broker starts.
        run('docker', 'run', '--rm', '--user', '0', '-v', str(broker)+':/work',
            'eclipse-mosquitto:2.0', 'sh', '-c',
            'mosquitto_passwd -U /work/passwords && chmod 644 /work/passwords')
    run('docker', 'compose', 'up', '-d', '--wait', '--wait-timeout', '180', 'mosquitto', 'influxdb')
    for attempt in range(60):
        try:
            org = api('/api/v2/orgs?org=group18', env)['orgs'][0]['id']
            bucket = api('/api/v2/buckets?name=telemetry', env)['buckets'][0]['id']
            break
        except (urllib.error.URLError, KeyError, IndexError):
            time.sleep(2)
    else:
        raise RuntimeError('InfluxDB onboarding did not finish. Credentials are preserved; rerun setup.')
    for key, action, description in [
        ('INFLUX_READ_TOKEN', 'read', 'Group18 Grafana read-only'),
        ('INFLUX_WRITE_TOKEN', 'write', 'Group18 Telegraf write-only')]:
        if not env[key]:
            created = api('/api/v2/authorizations', env, {'orgID': org,
                'description': description, 'permissions': [
                    {'action': action, 'resource': {'type': 'buckets', 'id': bucket, 'orgID': org}}]})
            env[key] = created['token']
            save_env(env)
    run('docker', 'compose', 'up', '-d', '--wait', '--wait-timeout', '180')
    print('Local services started. Run: python3 scripts/verify.py')
    print('Grafana: http://localhost:3000/d/group18-glasses')
    print('Private passwords: .env (not printed, excluded from Git).')


if __name__ == '__main__':
    main()
