from pathlib import Path
import struct
import tempfile
import unittest
import native_verify as verify


class LayoutTests(unittest.TestCase):
    def image(self, limits=None, addresses=None):
        limits = verify.LIMITS if limits is None else limits
        data = bytearray(64 + 56 * len(limits))
        data[:6] = b'\x7fELF\x02\x01'
        struct.pack_into('<Q', data, 32, 64)
        struct.pack_into('<HH', data, 54, 56, len(limits))
        for index, size in enumerate(limits):
            address = addresses[index] if addresses else index * (256 << 20)
            struct.pack_into('<II6Q', data, 64 + index * 56, 1, 4, 0, address, 0, 0, size, 1)
        return data

    def inspect(self, data):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'fixture.elf'
            path.write_bytes(data)
            return verify.layout(path)

    def test_exact_unchanged_caps_are_accepted(self):
        self.assertEqual([r['memsz'] for r in self.inspect(self.image())], verify.LIMITS)
        self.assertEqual(verify.LIMITS, [71 << 20, 26 << 20, 2 << 20, 25 << 20, 3 << 20])

    def test_each_cap_rejects_one_byte_over(self):
        for index in range(5):
            limits = verify.LIMITS.copy()
            limits[index] += 1
            with self.assertRaisesRegex(ValueError, 'size limit exceeded'):
                self.inspect(self.image(limits))

    def test_overlap_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Overlapping'):
            self.inspect(self.image(addresses=[0, 0, 512 << 20, 768 << 20, 1024 << 20]))

    def test_truncated_headers_rejected(self):
        with self.assertRaisesRegex(ValueError, 'program header'):
            self.inspect(self.image()[:-1])

    def test_wrong_load_count_rejected(self):
        with self.assertRaisesRegex(ValueError, 'five LOAD'):
            self.inspect(self.image(verify.LIMITS[:4]))


if __name__ == '__main__':
    unittest.main()
