#!/usr/bin/env python3
"""Device-rendered artwork, shared navigation, badge interactions and lifecycle."""
import argparse
import json
from pathlib import Path
import time
from device import capture, serial_open


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    records = []
    try:
        with serial_open(args.port) as wire:
            def send(command, wait=0.3):
                wire.write((command + '\n').encode())
                time.sleep(wait)
                response = wire.read(wire.in_waiting).decode(errors='replace')
                records.append({'command': command, 'response': response})
                print(command, response.strip(), flush=True)
                return response

            def status(page=None, sleep=None, theme=None, pinned=None):
                response = send('status')
                lines = response.splitlines()
                state = dict(item.split('=') for item in next(s for s in lines if s.startswith('STATUS ')).split()[1:])
                if page is not None:
                    assert int(state['page']) == page, response
                if sleep is not None:
                    assert int(state['sleep']) == sleep, response
                badge = next((s for s in lines if s.startswith('BADGE ')), None)
                if theme is not None or pinned is not None:
                    assert badge, response
                    fields = dict(item.split('=') for item in badge.split()[1:])
                    if theme is not None:
                        assert int(fields['theme']) == theme, response
                    if pinned is not None:
                        assert int(fields['pinned']) == pinned, response
                return state

            send('page 0')
            for page in (1, 7, 8, 9, 0):
                send('test-swipe left')
                status(page=page, sleep=0)
            send('test-swipe right')
            status(page=9)
            send('test-swipe up')
            status(page=2)
            send('test-button boot')
            status(page=9)
            for page, name in ((7, 'orbit'), (8, 'studio'), (9, 'pulse')):
                send(f'page {page}', 1.2)
                capture(wire, args.output / f'{name}.png')
                send('test-button pwr')
                status(page=page, sleep=1)
                send('test-button pwr')
                status(page=page, sleep=0)

            send('page 2')
            capture(wire, args.output / 'launcher.png')
            # Badge button is at (160,381), with the real sensor mirror applied.
            send('test-raw-tap 306 85')
            status(page=10)
            initial = send('status')
            current = int(initial.split('BADGE theme=')[1].split()[0])
            for _ in range(current):
                send('test-swipe right')
            status(theme=0, pinned=0)
            for theme, name in enumerate(('mochi', 'beep', 'little-orbit')):
                if theme:
                    send('test-swipe left')
                status(theme=theme)
                capture(wire, args.output / f'badge-{name}.png')
                send('test-raw-tap 233 233')
                # Capture temporarily owns the GUI loop. Wait for pointer release
                # processing, not just for the serial command to be acknowledged.
                deadline = time.monotonic() + 3
                while True:
                    response = send('status')
                    if f'reactions={theme + 1}' in response:
                        break
                    assert time.monotonic() < deadline, response
                capture(wire, args.output / f'badge-{name}-happy.png')

            send('test-raw-tap 233 72', 1.2)  # Display (233,394): stay-on toggle.
            state = status(pinned=1)
            timeout = int(state['timeout']) / 1000
            deadline = time.monotonic() + timeout + 1
            while time.monotonic() < deadline:
                time.sleep(min(2, max(0, deadline - time.monotonic())))
                status(page=10, sleep=0, pinned=1)
            send('test-button pwr')
            status(sleep=1, pinned=1)
            send('test-button boot')
            status(page=10, sleep=0, pinned=1)
            send('test-swipe down')
            state = status(page=2)
            # Leaving Badge must release the wake lock.
            time.sleep(int(state['timeout']) / 1000 + 0.5)
            status(page=2, sleep=1)
            send('test-button boot')
            status(page=2, sleep=0)
            send('page 10')
            status(theme=2, pinned=0)
            send('page 0')
            heaps = []
            for _ in range(8):
                for page in (7, 8, 9, 10, 2, 0):
                    send(f'page {page}', 0.15)
                heaps.append(int(status(page=0)['heap']))
            assert max(heaps[2:]) - min(heaps[2:]) < 2048, heaps
            records.append({'heap_after_cycles': heaps})
            send('page 7', 1.2)
            records.append({'result': 'PASS'})
    finally:
        (args.output / 'artwork-regression.json').write_text(json.dumps(records, indent=2) + '\n')


if __name__ == '__main__':
    main()
