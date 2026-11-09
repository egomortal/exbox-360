import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock
import zlib
from PIL import Image
import splash_producer as splash


class SplashTests(unittest.TestCase):
    def test_missing_input_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaisesRegex(ValueError, 'supported original'):
                splash.generate(Path(folder) / 'missing.png')

    def test_wrong_size_input_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'splash_360.png'
            path.write_bytes(b'not an original')
            with self.assertRaisesRegex(ValueError, 'supported original'):
                splash.generate(path)

    def test_same_size_wrong_hash_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'splash_360.png'
            path.write_bytes(b'x' * splash.INPUT_BYTES)
            with self.assertRaisesRegex(ValueError, 'SHA256 differs'):
                splash.generate(path)

    def test_bad_geometry_rejected(self):
        with self.assertRaises(ValueError):
            splash.compose(Image.new('RGBA', (2, 2)), (1280, 720))

    def test_center_aspect_and_corners_retained(self):
        source = Image.new('RGBA', splash.INPUT_SIZE, (120, 130, 140, 255))
        for size in splash.OUTPUTS.values():
            image, (x, y, width, height) = splash.compose(source, size)
            self.assertEqual(image.size, size)
            self.assertEqual(image.mode, 'RGB')
            self.assertLessEqual(abs(width / height - source.width / source.height), 0.002)
            self.assertLessEqual(abs(2 * x + width - size[0]), 1)
            self.assertLessEqual(abs(2 * y + height - size[1]), 1)
            self.assertEqual(image.getpixel((0, 0)), (255, 255, 255))
            for point in [(x, y), (x+width-1, y), (x, y+height-1), (x+width-1, y+height-1)]:
                self.assertEqual(image.getpixel(point), (120, 130, 140))

    def test_transparency_flattens_to_white(self):
        image, rect = splash.compose(Image.new('RGBA', splash.INPUT_SIZE, (0, 0, 0, 0)), (1920, 1080))
        self.assertEqual(image.getextrema(), ((255, 255),) * 3)

    def test_png_contract_roundtrip_and_determinism(self):
        image, rect = splash.compose(Image.new('RGBA', splash.INPUT_SIZE, (35, 70, 105, 255)), (1920, 1080))
        data = splash.encode_png(image)
        self.assertEqual(data, splash.encode_png(image))
        self.assertEqual(data[24:29], bytes((8, 2, 0, 0, 0)))
        with Image.open(io.BytesIO(data)) as decoded:
            self.assertEqual(decoded.size, image.size)
            self.assertEqual(decoded.tobytes(), image.tobytes())
        offset, compressed = 8, b''
        while offset < len(data):
            size = struct.unpack_from('>I', data, offset)[0]
            kind = data[offset+4:offset+8]
            payload = data[offset+8:offset+8+size]
            expected = struct.unpack_from('>I', data, offset+8+size)[0]
            self.assertEqual(zlib.crc32(kind+payload) & 0xffffffff, expected)
            if kind == b'IDAT': compressed += payload
            offset += 12 + size
        raw = zlib.decompress(compressed)
        self.assertEqual(len(raw), (image.width*3+1)*image.height)
        self.assertTrue(all(raw[y*(image.width*3+1)] == 0 for y in range(image.height)))

    def test_encoded_mode_rejected(self):
        with self.assertRaises(ValueError):
            splash.encode_png(Image.new('RGBA', (1920, 1080)))

    def test_environment_hash_mismatch_rejected(self):
        with mock.patch.object(splash, 'read_logo', return_value=Image.new('RGBA', splash.INPUT_SIZE)), mock.patch.object(splash, 'EXPECTED', {'incorrect': '0' * 64}):
            with self.assertRaisesRegex(ValueError, 'environment differs'):
                splash.generate('unused')


if __name__ == '__main__':
    unittest.main()
