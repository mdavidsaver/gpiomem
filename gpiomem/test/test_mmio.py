import struct
import unittest
import logging

from .. import mmio

_log = logging.getLogger(__name__)

class TestIO(unittest.TestCase):
    def setUp(self):
        from tempfile import NamedTemporaryFile
        self.file = NamedTemporaryFile()
        for n in range(0x200/4):
            self.file.write(struct.pack('<I', 0x2bad0000+n))

    def tearDown(self):
        self.file.close()

    def test_mmio(self):
        self.file.seek(0)
        self.file.write(struct.pack("IHHBBBB", 0x12345678, 0x0102, 0x0304, 0x50, 0x60, 0x70, 0x80))
        self.file.seek(0)
        self.file.flush()

        with mmio.MMIO(self.file.name, 12) as io:
            self.assertListEqual(io.io([
                ( 0, 32, 0, 0),
                ( 4, 16, 0, 0),
                ( 6, 16, 0, 0),
                ( 8,  8, 0, 0),
                ( 9,  8, 0, 0),
                (10,  8, 0, 0),
                (11,  8, 0, 0),
            ]), [
                0x12345678, 0x0102, 0x0304, 0x50, 0x60, 0x70, 0x80,
            ])

            io.width = 32
            self.assertEqual(io[0], 0x12345678)
            io.width = 16
            self.assertEqual(io[4], 0x0102)
            self.assertEqual(io[6], 0x0304)
            io.width = 8
            self.assertEqual(io[8], 0x50)

            io.width = 32
            io[0] = 0x0abc0def
            self.assertEqual(io[0], 0x0abc0def)

            io.width = 16
            io[0] = 0xabcd
            self.assertEqual(io[0], 0xabcd)

            io.width = 8
            io[0] = 0xcf
            self.assertEqual(io[0], 0xcf)
