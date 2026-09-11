#!/usr/bin/env python3
"""Decode device-rendered QR codes and exercise portal lifecycle. Requires zxing-cpp."""
import argparse
import json
from pathlib import Path
import re
import tempfile
import time

from PIL import Image
import zxingcpp
from device import serial_open, capture


def run(wire, output, leave_open=False):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    records = []

    def send(command, wait=0.8):
        wire.write((command + '\n').encode())
        time.sleep(wait)
        reply = wire.read(wire.in_waiting).decode(errors='replace')
        records.append({'command': command, 'response': reply})
        return reply

    def state(**expected):
        deadline = time.monotonic() + 8
        while True:
            reply = send('status')
            groups = {}
            for line in reply.splitlines():
                if line.startswith(('STATUS ', 'PHOTO ')):
                    name, *values = line.split()
                    groups[name] = dict(v.split('=', 1) for v in values)
            if 'STATUS' in groups and 'PHOTO' in groups and all(groups['PHOTO'].get(k) == str(v) for k, v in expected.items()):
                return groups
            assert time.monotonic() < deadline, reply

    def start(image):
        if image == '1':
            send('test-tap 233 233', 1.2)
        send('test-tap 233 390', 1.2)
        state(wifi=1, qr='wifi')

    try:
        send('page 15', 3)
        initial = state(wifi=0)
        image = initial['PHOTO']['image']
        heaps = []
        previous = None
        for cycle in range(4):
            start(image)
            # Wi-Fi QR contains a live password: decode in a private temporary
            # directory, retain only the result, and remove the captured frame.
            with tempfile.TemporaryDirectory(prefix='infinitime-private-qr-') as temporary:
                frame = Path(temporary) / 'wifi.png'
                capture(wire, frame)
                decoded = zxingcpp.read_barcode(Image.open(frame))
                assert decoded is not None, 'Wi-Fi QR not decodable'
                assert re.fullmatch(r'WIFI:T:WPA;S:InfiniTime-Badge;P:[0-9a-f]{8};;', decoded.text), 'Unexpected Wi-Fi QR format'
                assert decoded.text != previous, 'AP session credential did not rotate'
                previous = decoded.text
                # Do not print or record decoded.text, including in assertions.
            send('test-tap 233 350', 1.2)
            state(qr='page')
            if cycle == 0:
                capture(wire, output / 'photo-page-qr.png')
                decoded = zxingcpp.read_barcode(Image.open(output / 'photo-page-qr.png'))
                assert decoded is not None and decoded.text == 'http://192.168.4.1/', 'Wrong page QR'
            send('test-tap 233 350', 1.2)
            state(qr='wifi')
            send('test-tap 233 233', 1.2)  # Tapping the QR must not hide its controls.
            state(qr='wifi')
            send('test-tap 233 390', 1.2)
            current = state(wifi=0, image=image)
            heaps.append(int(current['STATUS']['heap']))
            print(f'Cycle {cycle + 1}: QR decode, switch, AP/DNS close PASS', flush=True)
        assert max(heaps[1:]) - min(heaps[1:]) < 2048, heaps
        records.append({'heap_after_portal_cycles': heaps})
        if leave_open:
            start(image)
        else:
            send('test-button boot')
        records.append({'result': 'PASS'})
    finally:
        (output / 'photo-qr-regression.json').write_text(json.dumps(records, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--leave-open', action='store_true')
    args = parser.parse_args()
    with serial_open(args.port) as wire:
        run(wire, args.output, args.leave_open)
