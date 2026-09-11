"""Exercise failure paths in the binary framebuffer transport."""
import io
from pathlib import Path
import tempfile
import unittest
import zlib
from unittest.mock import patch
import device


class Wire:
    def __init__(self, payload):
        self.stream = io.BytesIO(payload)
        self.writes = []

    def read(self, size):
        # USB can return fewer bytes than requested.
        return self.stream.read(min(size, 137))

    def readline(self):
        return self.stream.readline()

    def write(self, data):
        self.writes.append(data)
        return len(data)

    def reset_input_buffer(self):
        pass


class TransportTests(unittest.TestCase):
    @patch('device.time.sleep')
    def test_rgb565_channels_and_fragmented_usb(self, _):
        from PIL import Image
        raw = b'\x00\xf8\xe0\x07\x1f\x00' + bytes(466 * 466 * 2 - 6)
        payload = bytearray(b'PAGE 0\nFRAME 466 466 434312\n')
        for offset in range(0, len(raw), 1024):
            chunk = raw[offset:offset + 1024]
            payload.extend(f'CHUNK {offset} {len(chunk)} {zlib.crc32(chunk):08x}\n'.encode() + chunk)
        payload.extend(b'\nFRAME_END\n')
        wire = Wire(payload)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'frame.png'
            device.capture(wire, path)
            with Image.open(path) as frame:
                self.assertEqual(frame.size, (466, 466))
                self.assertEqual([frame.getpixel((x, 0)) for x in range(3)],
                                 [(255, 0, 0), (0, 255, 0), (0, 0, 255)])

    @patch('device.time.sleep')
    def test_corruption_and_duplicate_chunk(self, _):
        raw = bytes(466 * 466 * 2)
        payload = bytearray(b'FRAME 466 466 434312\n')
        # CRC mismatch, retransmission, then a duplicate after a lost ACK.
        payload.extend(b'CHUNK 0 1024 00000000\n' + raw[:1024])
        first = f'CHUNK 0 1024 {zlib.crc32(raw[:1024]):08x}\n'.encode() + raw[:1024]
        payload.extend(first + first)
        for offset in range(1024, len(raw), 1024):
            chunk = raw[offset:offset + 1024]
            payload.extend(f'CHUNK {offset} {len(chunk)} {zlib.crc32(chunk):08x}\n'.encode() + chunk)
        payload.extend(b'\nFRAME_END\n')
        wire = Wire(payload)
        with tempfile.TemporaryDirectory() as folder:
            device.capture(wire, Path(folder) / 'frame.png')
        self.assertIn(b'NACK 0\n', wire.writes)
        self.assertEqual(wire.writes.count(b'ACK 0\n'), 2)

    @patch('device.time.sleep')
    def test_truncated_frame_never_saved(self, _):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'frame.png'
            with self.assertRaisesRegex(RuntimeError, 'Truncated'):
                device.capture(Wire(b'FRAME 466 466 434312\nCHUNK 0 1024 00000000\nshort'), path)
            self.assertFalse(path.exists())

    @patch('device.time.sleep')
    def test_wrong_dimensions_rejected(self, _):
        with self.assertRaisesRegex(RuntimeError, 'Unexpected frame'):
            device.capture(Wire(b'FRAME 240 240 115200\n'), Path('unused.png'))


if __name__ == '__main__':
    unittest.main()
