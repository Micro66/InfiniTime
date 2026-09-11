#!/usr/bin/env python3
"""USB maintenance for this port. pip install esptool==4.8.1 pyserial pillow.
Backups contain private settings; store them outside the repository.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import zlib


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def esptool(port, *args):
    subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32s3',
                    '--port', port, '--baud', '921600', *map(str, args)], check=True)


def serial_open(port):
    import serial
    connection = serial.Serial()
    connection.port = port
    connection.baudrate = 115200
    connection.timeout = 5
    connection.dtr = False
    connection.rts = False
    connection.open()
    # macOS may toggle USB Serial/JTAG reset lines when opening the port.
    # Wait for the application, not just the boot ROM, before issuing commands.
    connection.timeout = 0.2
    boot_log = bytearray()
    deadline = time.monotonic() + 8
    last_probe = 0
    while time.monotonic() < deadline:
        if time.monotonic() - last_probe >= 0.25:
            connection.write(b"status\n")
            last_probe = time.monotonic()
        line = connection.readline()
        boot_log.extend(line)
        if line.startswith(b"STATUS "):
            connection.timeout = 5
            connection.reset_input_buffer()
            connection.startup_log = bytes(boot_log)
            return connection
    connection.close()
    raise RuntimeError("Firmware not ready:\n" + boot_log.decode(errors="replace"))


def read_exact(connection, count):
    data = bytearray()
    while len(data) < count:
        chunk = connection.read(count - len(data))
        if not chunk:
            raise RuntimeError(f'Truncated frame: {len(data)}/{count} bytes')
        data.extend(chunk)
    return bytes(data)


def capture(connection, output):
    from PIL import Image
    connection.write(b'wake\n')
    time.sleep(0.2)
    connection.reset_input_buffer()
    connection.write(b'capture\n')
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        header = connection.readline()
        if header.startswith(b'FRAME '):
            _, width, height, length = header.split()
            width, height, length = int(width), int(height), int(length)
            if (width, height, length) != (466, 466, 466 * 466 * 2):
                raise RuntimeError(f'Unexpected frame: {header!r}')
            raw = bytearray(length)
            received = 0
            while received < length:
                block = connection.readline().split()
                if os.environ.get("INFINITIME_DEBUG"):
                    print(round(time.monotonic(), 3), block, "expected", received, flush=True)
                if len(block) != 4 or block[0] != b'CHUNK':
                    raise RuntimeError(f'Invalid chunk header: {block!r}')
                offset, size, crc = int(block[1]), int(block[2]), int(block[3], 16)
                if offset > received or not 0 < size <= 1024 or offset + size > length or (offset < received and offset + size > received):
                    raise RuntimeError(f'Out-of-order or oversized frame chunk: {block!r}, expected {received}')
                data = read_exact(connection, size)
                if zlib.crc32(data) != crc:
                    connection.write(f'NACK {offset}\n'.encode())
                    continue
                raw[offset:offset + size] = data
                connection.write(f'ACK {offset}\n'.encode())
                received = max(received, offset + size)
            if connection.readline().strip() or connection.readline().strip() != b'FRAME_END':
                raise RuntimeError('Missing frame terminator')
            rgb = bytearray(width * height * 3)
            for i in range(width * height):
                pixel = raw[i * 2] | raw[i * 2 + 1] << 8
                rgb[i*3:i*3+3] = bytes(((pixel >> 11) * 255 // 31,
                                       ((pixel >> 5) & 63) * 255 // 63,
                                       (pixel & 31) * 255 // 31))
            output.parent.mkdir(parents=True, exist_ok=True)
            Image.frombytes('RGB', (width, height), bytes(rgb)).save(output)
            return
    raise RuntimeError('Firmware did not return a frame')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    commands = parser.add_subparsers(dest='operation', required=True)
    commands.add_parser('inspect')
    for operation in ('backup', 'verify-backup', 'restore'):
        command = commands.add_parser(operation)
        command.add_argument('file', type=Path)
    commands.add_parser('flash')
    screenshot = commands.add_parser('capture')
    screenshot.add_argument('file', type=Path)
    send = commands.add_parser('send')
    send.add_argument('command')
    commands.add_parser('sync-time')
    args = parser.parse_args()
    if args.operation == 'inspect':
        esptool(args.port, 'flash_id')
    elif args.operation in ('backup', 'verify-backup'):
        if args.operation == 'backup':
            if args.file.exists():
                parser.error('Refusing to overwrite an existing backup')
            args.file.parent.mkdir(parents=True, exist_ok=True)
            esptool(args.port, '--after', 'no_reset', 'read_flash', 0, 0x2000000, args.file)
        if args.file.stat().st_size != 0x2000000:
            parser.error('Expected a complete 32 MiB backup')
        esptool(args.port, 'verify_flash', 0, args.file)
        manifest = {'bytes': args.file.stat().st_size, 'sha256': digest(args.file),
                    'verified_against_device_at': datetime.datetime.now(datetime.timezone.utc).isoformat()}
        args.file.with_suffix('.json').write_text(json.dumps(manifest, indent=2) + '\n')
    elif args.operation == 'restore':
        manifest = json.loads(args.file.with_suffix('.json').read_text())
        if args.file.stat().st_size != 0x2000000 or digest(args.file) != manifest['sha256']:
            parser.error('Backup is incomplete or checksum does not match')
        esptool(args.port, 'write_flash', 0, args.file)
    elif args.operation == 'flash':
        folder = Path(__file__).resolve().parents[1] / '.pio/build/waveshare-175'
        images = [(0, folder/'bootloader.bin'), (0x8000, folder/'partitions.bin'), (0x10000, folder/'firmware.bin')]
        for _, path in images:
            if not path.is_file():
                parser.error(f'Build first: missing {path}')
        esptool(args.port, 'write_flash', '--flash_size', '32MB',
                *[item for address, path in images for item in (hex(address), path)])
    else:
        with serial_open(args.port) as connection:
            if args.operation == 'capture':
                capture(connection, args.file)
            else:
                if args.operation == 'sync-time':
                    china = datetime.timezone(datetime.timedelta(hours=8))
                    command = datetime.datetime.now(china).strftime('time %Y-%m-%d %H:%M:%S')
                else:
                    command = args.command
                connection.reset_input_buffer()
                connection.write((command + '\n').encode())
                time.sleep(0.25)
                print(connection.read(connection.in_waiting).decode(errors='replace'), end='')


if __name__ == '__main__':
    main()
