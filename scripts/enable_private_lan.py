#!/usr/bin/env python3
"""Bind only MQTT to one explicit private LAN IP and issue a matching certificate."""
import ipaddress
import json
import os
import subprocess
import sys
from setup import ROOT, load_env, save_env, run

address = str(ipaddress.ip_address(sys.argv[1]))
parsed = ipaddress.ip_address(address)
if not parsed.is_private or parsed.is_loopback or parsed.is_unspecified or parsed.is_link_local:
    raise SystemExit('Provide a specific private LAN address, not a wildcard/public/loopback address.')
os.chdir(ROOT)
ca = ROOT/'.local/ca'
broker = ROOT/'.local/mosquitto'
(ca/'server.ext').write_text('subjectAltName=DNS:localhost,DNS:mosquitto,IP:127.0.0.1,IP:'+address+'\n'
                            'extendedKeyUsage=serverAuth\nbasicConstraints=CA:FALSE\n')
subprocess.run(['openssl','x509','-req','-in',str(ca/'server.csr'),'-CA',str(broker/'ca.crt'),
    '-CAkey',str(ca/'ca.key'),'-CAcreateserial','-out',str(broker/'server.crt'),'-days','365',
    '-extfile',str(ca/'server.ext')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
env=load_env()
env['MQTT_BIND_ADDRESS']=address
save_env(env)
path=ROOT/'.local/wifi.json'
wifi=json.loads(path.read_text())
wifi['mqtt_host']=address
path.write_text(json.dumps(wifi,indent=2)+'\n')
path.chmod(0o600)
run('docker','compose','up','-d','--force-recreate','mosquitto')
print('MQTT TLS enabled on '+address+':8883. Database and dashboard remain localhost-only.')
