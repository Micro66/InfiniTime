#!/usr/bin/env python3
"""Exercise queued button events before the real loop's screen timeout check.

Uses the physical buttons' handlers, but does not synthesize GPIO or PMIC IRQs.
No capture/wake helper is used to observe the result of a button wake.
"""
import argparse
import json
from pathlib import Path
import re
import time
from device import serial_open


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    records = []
    stale_samples = 0
    args.output.mkdir(parents=True, exist_ok=True)
    try:
        with serial_open(args.port) as wire:
            def send(command):
                wire.write((command + '\n').encode())
                time.sleep(0.3)
                result = wire.read(wire.in_waiting).decode(errors='replace')
                records.append({'command': command, 'response': result})
                print(command, result.strip(), flush=True)
                return result

            def status(sleeping=None, page=None):
                response = send('status')
                lines = [line for line in response.splitlines() if line.startswith('STATUS ')]
                assert lines, 'Missing device status'
                fields = dict(item.split('=') for item in lines[-1].split()[1:])
                if sleeping is not None:
                    assert int(fields['sleep']) == sleeping, fields
                if page is not None:
                    assert int(fields['page']) == page, fields
                return fields

            def press(button):
                nonlocal stale_samples
                response = send('test-button ' + button)
                match = re.search(r'TEST BUTTON ' + button + r' sampled=(\d+) activity=(\d+)', response)
                assert match, 'Queued event did not reach button handler'
                sampled, activity = map(int, match.groups())
                if sampled < activity:
                    stale_samples += 1

            send('page 0')
            time.sleep(0.2)  # Leave the LVGL recent-touch activity window.
            for _ in range(10):
                press('pwr')
                status(sleeping=1, page=0)
                press('pwr')
                status(sleeping=0, page=0)
            press('pwr')
            status(sleeping=1)
            press('boot')
            status(sleeping=0, page=0)
            time.sleep(2)
            status(sleeping=0)
            press('boot')
            status(sleeping=0, page=2)
            press('boot')
            status(sleeping=0, page=0)

            # Wait for the configured timeout without changing persisted settings.
            press('pwr')
            status(sleeping=1)
            press('boot')
            timeout = int(status(sleeping=0)['timeout']) / 1000
            started = time.monotonic()
            while time.monotonic() - started < timeout - 1:
                time.sleep(min(2, max(0, timeout - 1 - (time.monotonic() - started))))
                status(sleeping=0)
            deadline = started + timeout + 3
            while int(status()['sleep']) == 0:
                assert time.monotonic() < deadline, 'Automatic timeout did not fire'
                time.sleep(0.3)
            press('pwr')
            status(sleeping=0, page=0)
            time.sleep(2)
            status(sleeping=0, page=0)
            assert stale_samples > 0, 'Did not exercise a wake later than the old cached time'
            records.append({'result': 'PASS', 'stale_timestamp_cases': stale_samples})
            print(f'PASS; {stale_samples} events exercised the old stale-timestamp condition', flush=True)
    finally:
        (args.output / 'button-regression.json').write_text(json.dumps(records, indent=2) + '\n')


if __name__ == '__main__':
    main()
