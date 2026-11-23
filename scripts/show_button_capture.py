#!/usr/bin/env python3
"""Monitor button capture and save a length-checked JPEG preview locally."""
import argparse
from pathlib import Path
from datetime import datetime
import serial
p=argparse.ArgumentParser()
p.add_argument('--port',required=True)
a=p.parse_args()
out=Path(__file__).resolve().parents[1]/'.local'/'captures'
out.mkdir(parents=True,exist_ok=True)
expected=None
chunks=[]
with serial.Serial(a.port,115200,timeout=1) as device:
    print('LIVE BUTTON CAPTURE: release button, press to start, press again to stop.',flush=True)
    while True:
        line=device.readline().decode('ascii',errors='replace').strip()
        if not line: continue
        if line.startswith('PREVIEW_JPEG_BEGIN '):
            expected=int(line.split()[-1]); chunks=[]
            if not 0<expected<=1000000: raise ValueError('Invalid preview length')
        elif line=='PREVIEW_JPEG_END' and expected is not None:
            data=bytes.fromhex(''.join(chunks))
            if len(data)!=expected or not data.startswith(b'\xff\xd8') or not data.endswith(b'\xff\xd9'):
                raise ValueError('Incomplete JPEG preview')
            path=out/(datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'.jpg')
            path.write_bytes(data)
            print('CAPTURED IMAGE: '+str(path),flush=True)
            expected=None
        elif expected is not None:
            chunks.append(line)
            if sum(map(len,chunks))>expected*2: raise ValueError('Oversized preview')
        else: print(line,flush=True)
