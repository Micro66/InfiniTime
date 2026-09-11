#!/usr/bin/env python3
"""Exercise pocket apps on the board without replacing/capturing the user's photo."""
import argparse
import json
from pathlib import Path
import time
from device import capture, serial_open


def run(wire, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    records = []

    def send(command, wait=0.7):
        wire.write((command + '\n').encode())
        time.sleep(wait)
        response = wire.read(wire.in_waiting).decode(errors='replace')
        deadline = time.monotonic() + 4
        while not response and time.monotonic() < deadline:
            time.sleep(0.1)
            response += wire.read(wire.in_waiting).decode(errors='replace')
        records.append({'command': command, 'response': response})
        print(command, response.strip(), flush=True)
        return response

    def status(**expected):
        deadline = time.monotonic() + 6
        while True:
            response = send('status')
            groups = {}
            for line in response.splitlines():
                if line.startswith(('STATUS ', 'PLAY ', 'GARDEN ', 'DICE ', 'MARBLE ', 'PHOTO ')):
                    name, *fields = line.split()
                    groups[name] = dict(field.split('=', 1) for field in fields)
            ready = all(name in groups for name in ('STATUS', 'PLAY', 'GARDEN'))
            for key, value in expected.items():
                name, field = key.split('__')
                ready = ready and groups.get(name, {}).get(field) == str(value)
            if ready:
                return groups
            assert time.monotonic() < deadline, response

    def tap(x, y):
        send(f'test-raw-tap {466-x} {466-y}', 1.2)

    try:
        send('wake')
        initial = status()
        # Obtain selected watch face without changing that preference.
        send('page 2')
        send('test-button boot')
        watch = int(status()['STATUS']['page'])
        send('page 2')
        sheet = int(status()['PLAY']['sheet'])
        for _ in range((3 - sheet) % 3):
            send('test-swipe left')
        status(STATUS__page=2, PLAY__sheet=0)
        tap(160, 381)
        status(PLAY__sheet=1)
        capture(wire, output / 'launcher-play.png')
        time.sleep(0.5)
        tap(319, 168)
        state = status(STATUS__page=11, PLAY__motion=1)
        serial = int(state['PLAY']['samples'])
        rolls = int(state['DICE']['rolls'])
        tap(233, 233)
        state = status()
        assert int(state['DICE']['rolls']) > rolls, state
        assert int(state['PLAY']['samples']) > serial, state
        assert 1 <= int(state['DICE']['value']) <= 6, state
        capture(wire, output / 'dice.png')
        for mode in ('fortune', 'yes-no', 'dice'):
            tap(233, 395)
            tap(233, 233)
            state = status()
            assert 1 <= int(state['DICE']['value']) <= (2 if mode == 'yes-no' else 6), state
            if mode != 'dice':
                capture(wire, output / f'{mode}.png')
        send('test-button boot')
        status(STATUS__page=2, PLAY__motion=0)
        tap(147, 290)
        status(STATUS__page=12, PLAY__motion=1)
        tap(233, 233)
        state = status()
        assert ((float(state['MARBLE']['x'])-233)**2 + (float(state['MARBLE']['y'])-233)**2)**0.5 <= 184
        capture(wire, output / 'gravity.png')
        send('test-swipe down')
        status(STATUS__page=2, PLAY__motion=0)
        tap(319, 290)
        state = status(STATUS__page=13, PLAY__audio=1)
        blocks = int(state['PLAY']['blocks'])
        time.sleep(1)
        assert int(status()['PLAY']['blocks']) > blocks
        tap(233, 233)
        capture(wire, output / 'sound-buddy.png')
        for _ in range(3):
            send('test-button pwr')
            status(STATUS__page=13, STATUS__sleep=1)
            send('test-button pwr')
            status(STATUS__page=13, STATUS__sleep=0)
        timeout = int(state['STATUS']['timeout']) / 1000
        time.sleep(timeout + 0.5)
        status(STATUS__sleep=0)
        send('test-button boot')
        status(STATUS__page=2, PLAY__audio=0)
        time.sleep(timeout + 0.5)
        status(STATUS__sleep=1)
        send('test-button boot')
        status(STATUS__page=2, STATUS__sleep=0)
        tap(160, 381)
        status(PLAY__sheet=2)
        capture(wire, output / 'launcher-photo.png')
        time.sleep(0.5)
        tap(147, 168)
        state = status(STATUS__page=14)
        # Leave an existing user timer untouched. Only exercise a ready timer.
        if state['GARDEN']['phase'] == '0' and state['GARDEN']['running'] == '0':
            original = state['GARDEN'].copy()
            tap(300, 390)
            started = status(GARDEN__running=1)
            send('page 2')
            time.sleep(2)
            later = status()
            assert int(later['GARDEN']['remaining']) < int(started['GARDEN']['remaining'])
            send('page 14')
            tap(300, 390)
            paused = status(GARDEN__running=0)['GARDEN']['remaining']
            time.sleep(2)
            status(GARDEN__remaining=paused)
            tap(160, 390)
            assert status()['GARDEN'] == original
        else:
            records.append({'garden_interaction': 'skipped to preserve existing timer'})
        capture(wire, output / 'garden.png')
        send('test-button boot')
        tap(319, 168)
        state = status(STATUS__page=15, PHOTO__wifi=0)
        photo = state['PHOTO']['image']
        # No capture of Photo Badge: it may contain a private photo or password.
        if photo == '1':
            tap(233, 233)
        tap(233, 390)
        status(PHOTO__wifi=1)
        tap(233, 390)
        status(PHOTO__wifi=0, PHOTO__image=photo)
        send('test-button boot')
        heaps = []
        for _ in range(6):
            for page in (11, 12, 13):
                send(f'page {page}')
                state = status(STATUS__page=page)
                assert state['PLAY']['audio' if page == 13 else 'motion'] == '1'
            send('page 15')
            status(PHOTO__image=photo)
            if photo == '1':
                tap(233, 233)
            tap(233, 390)
            status(PHOTO__wifi=1)
            # Exit with the AP active: its destructor must stop server/radio.
            send('test-button boot', 1.2)
            state = status(STATUS__page=2, PLAY__audio=0, PLAY__motion=0)
            heaps.append(int(state['STATUS']['heap']))
        assert max(heaps[2:]) - min(heaps[2:]) < 2048, heaps
        records.append({'heap_after_sensor_radio_cycles': heaps})
        send(f'page {watch}')
        status(STATUS__page=watch, STATUS__timeout=initial['STATUS']['timeout'])
        records.append({'result': 'PASS'})
    finally:
        (output / 'play-regression.json').write_text(json.dumps(records, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with serial_open(args.port) as wire:
        run(wire, args.output)


if __name__ == '__main__':
    main()
