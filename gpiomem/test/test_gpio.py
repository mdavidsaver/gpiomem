
import struct
import unittest

from .. import GPIOMEM

class TestIO(unittest.TestCase):
    def setUp(self):
        from tempfile import NamedTemporaryFile
        self.file = NamedTemporaryFile()
        for n in range(0x200/4):
            self.file.write(struct.pack('<I', 0x2bad0000+n))
        self.file.flush()

        self.IO = GPIOMEM(self.file.name)

    def tearDown(self):
        self.file.close()

    def test_npins(self):
        self.assertEqual(self.IO.npins, 54)

    def test_getalt(self):
        self.file.seek(0)
        self.file.write(struct.pack('<I', 0x1f803f))
        self.file.flush()

        self.assertEqual(self.IO.getalt([0, 1, 6, 5]), [7, 7, 7, 7])

        self.file.seek(0)
        self.file.write(struct.pack('<I', 0x12345678))
        self.file.flush()

        self.assertEqual(self.IO.getalt([0, 1, 6, 5]), [0, 7, 5, 0])

    def test_setalt(self):
        self.file.seek(0)
        self.file.write(struct.pack('<I', 0x12345678))
        self.file.write(struct.pack('<I', 0xabcdef01))
        self.file.write(struct.pack('<I', 0x02030405))
        self.file.write(struct.pack('<I', 0x06070809))
        self.file.write(struct.pack('<I', 0x0a0b0c0d))
        self.file.write(struct.pack('<I', 0x0e0f1122))
        self.file.flush()

        self.IO.setalt([0, 11, 14], [7, 6, 0])
        self.IO._sync() # not needed for real I/O registers

        self.file.seek(0)
        self.assertEqual(struct.unpack('<I', self.file.read(4))[0], 0x1234567f)
        self.assertEqual(struct.unpack('<I', self.file.read(4))[0], 0xabcd8f31)
        self.assertEqual(struct.unpack('<I', self.file.read(4))[0], 0x02030405)
        self.assertEqual(struct.unpack('<I', self.file.read(4))[0], 0x06070809)
        self.assertEqual(struct.unpack('<I', self.file.read(4))[0], 0x0a0b0c0d)
        self.assertEqual(struct.unpack('<I', self.file.read(4))[0], 0x0e0f1122)

        self.assertEqual(self.IO.getalt([0, 11, 14]), [7, 6, 0])

    def test_out(self):
        self.file.seek(0x1c)
        self.file.write(struct.pack('<I', 0))
        self.file.write(struct.pack('<I', 0))
        self.file.seek(0x28)
        self.file.write(struct.pack('<I', 0))
        self.file.write(struct.pack('<I', 0))
        self.file.flush()

        self.IO.output([0, 4, 30, 31, 32, 40], [1, 1, 0, 1, 1, 1])
        self.IO._sync() # not needed for real I/O registers

        self.file.seek(0x1c)
        self.assertEqual(struct.unpack('<I', self.file.read(4))[0], 0x80000011)
        self.assertEqual(struct.unpack('<I', self.file.read(4))[0], 0x00000101)

        self.file.seek(0x28)
        self.assertEqual(struct.unpack('<I', self.file.read(4))[0], 0x40000000)
        self.assertEqual(struct.unpack('<I', self.file.read(4))[0], 0x00000000)
