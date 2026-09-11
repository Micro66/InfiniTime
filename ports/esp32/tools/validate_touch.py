#!/usr/bin/env python3
"""Test the configured SensorLib coordinate transform against real LVGL hit targets.
These are raw-coordinate injections, not physical finger contacts.
"""
import argparse
import json
from pathlib import Path
import time
from device import serial_open, capture

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', required=True)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
records = []
with serial_open(args.port) as wire:
    def send(command):
        wire.write((command + '\n').encode())
        time.sleep(0.3)
        result = wire.read(wire.in_waiting).decode(errors='replace')
        records.append({'command': command, 'response': result})
        print(command, result.strip(), flush=True)
        return result

    def page_is(page):
        assert f'page={page} ' in send('status'), f'Expected page {page}'

    send('page 0')
    # Physical Apps location (233,383) arrives as raw (233,83).
    send('test-raw-tap 233 83')
    page_is(2)
    # Top-left calculator target must be hit by bottom-right raw coordinates.
    send('test-raw-tap 319 298')
    page_is(3)
    for point in ('350 169', '116 229', '272 169', '116 108'):
        send('test-raw-tap ' + point)  # 1 + 2 =
    capture(wire, args.output / 'raw-touch-calculator.png')
    send('page 2')
    # Bottom-right settings target must be hit by top-left raw coordinates.
    send('test-raw-tap 147 176')
    page_is(6)
    capture(wire, args.output / 'raw-touch-settings.png')
    send('page 0')
(args.output / 'touch-regression.json').write_text(json.dumps(records, indent=2) + '\n')
