#!/usr/bin/env python3
"""On-device UI regression; injected events do not prove physical touch works."""
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
transcript = []
with serial_open(args.port) as wire:
    def send(command, wait=0.2):
        wire.write((command + '\n').encode())
        time.sleep(wait)
        result = wire.read(wire.in_waiting).decode(errors='replace')
        transcript.append({'command': command, 'response': result})
        print(command, result.strip(), flush=True)
        return result

    send('wake')
    for page, name in enumerate(('digital', 'analog', 'launcher', 'calculator', 'stopwatch', 'twos', 'settings')):
        send(f'page {page}', 1.2)
        capture(wire, args.output / f'{name}.png')
        send('status')
    send('page 3')
    # 1 + 2 = using the actual LVGL button matrix via injected pointer events.
    for x, y in ((116, 297), (350, 237), (194, 297), (350, 358)):
        send(f'test-tap {x} {y}', 0.3)
    capture(wire, args.output / 'calculator-1-plus-2.png')
    send('page 4')
    send('test-tap 331 350', 1.5)
    capture(wire, args.output / 'stopwatch-running.png')
    send('test-tap 331 350')
    send('page 5')
    for direction in ('left', 'up', 'right', 'down') * 4:
        send(f'test-swipe {direction}', 0.12)
    capture(wire, args.output / 'twos-played.png')
    send('page 0', 1.2)
    send('status')
    for cycle in range(10):
        for page in range(7):
            send(f'page {page}', 0.12)
        send('page 0', 0.15)
        send('status')
    send('sleep')
    assert 'sleep=1' in send('status'), 'Display did not enter off state'
    send('wake')
    assert 'sleep=0' in send('status'), 'Display did not wake'
    send('time 2026-02-30 12:00:00')
    assert 'TIME INVALID' in transcript[-1]['response'], 'Invalid calendar date accepted'
    send('page 0', 1.2)
    capture(wire, args.output / 'final-digital.png')
(args.output / 'serial-regression.json').write_text(json.dumps(transcript, indent=2) + '\n')
