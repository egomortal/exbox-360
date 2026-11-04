"""XUIZ ordering/byte preservation and truncated archive regression checks."""
from pathlib import Path
import struct,sys,unittest
sys.path[:0]=[str(Path(__file__).resolve().parents[1]/'tools'),str(Path(__file__).resolve().parents[1]/'tools/xur5')]
from xuiz_archive import members
from build_era_library import unpack


def archive(entries,version=2):
    header=bytearray(22);header[:4]=b'XUIZ';struct.pack_into('>I',header,4,version);struct.pack_into('>H',header,20,len(entries))
    table=bytearray();payload=bytearray()
    for name,data in entries:
        raw=name.encode('utf-16be' if version==1 else 'latin1')
        table+=struct.pack('>IIB',len(data),len(payload),len(raw)//(2 if version==1 else 1))+raw
        payload+=data
    return bytes(header+table+payload)


class ArchiveTests(unittest.TestCase):
    def test_versions_order_duplicates_and_binary_bytes(self):
        for version in (1,2):
            entries=[('folder\\image.png',bytes(range(256))),('../handles',b'\0\xff'),('repeat',b'first'),('repeat',b'last'),('empty',b'')]
            data=archive(entries,version)
            self.assertEqual(list(members(data)),entries)
            self.assertEqual(unpack(data),{n.replace('\\','/'):b for n,b in entries})
    def test_unicode_name(self):
        data=archive([('服装/😀',b'original')],1)
        self.assertEqual(list(members(data)),[('服装/😀',b'original')])
    def test_empty_table(self):
        self.assertEqual(list(members(archive([]))),[])
    def test_header_and_table_truncation(self):
        data=archive([('first',b'abc')])
        for length in range(22+9+len('first')):
            with self.assertRaises(ValueError):list(members(data[:length]))
        with self.assertRaises(ValueError):list(members(b'NOPE'+data[4:]))
    def test_validate_all_payloads_before_yield(self):
        data=archive([('valid',b'abc'),('truncated',b'xyz')])[:-1]
        with self.assertRaises(ValueError):next(members(data))
    def test_out_of_range_offset(self):
        data=bytearray(archive([('x',b'a')]))
        struct.pack_into('>I',data,26,0xffffffff)
        with self.assertRaises(ValueError):list(members(bytes(data)))
    def test_name_decode_rejects_invalid_surrogate(self):
        data=bytearray(archive([('a',b'x')],1));data[31:33]=b'\xd8\x00'
        with self.assertRaises(ValueError):list(members(bytes(data)))


if __name__=='__main__':unittest.main()
