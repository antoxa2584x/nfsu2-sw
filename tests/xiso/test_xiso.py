"""Synthetic discs only; no game data required. Run with python3 test_xiso.py."""
import ctypes as C
import pathlib
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASES = (0, 0xFD90, 0x30600, 0xFD90000, 0x18300000)

class Iso(C.Structure):
    _fields_ = [("fd", C.c_int), ("size", C.c_uint64), ("base", C.c_uint64),
                ("root_sector", C.c_uint32), ("root_size", C.c_uint32)]
VISITOR = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_char_p, C.c_uint64, C.c_uint32, C.c_int)

def entry(name, sector=34, size=4, directory=False):
    name = name.encode("ascii")
    value = struct.pack("<HHIIBB", 0, 0, sector, size, 0x10 if directory else 0, len(name)) + name
    return value + b"\0" * (-len(value) % 4)

class XisoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.folder = pathlib.Path(cls.temp.name)
        library = cls.folder / "xiso.dylib"
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                        str(ROOT / "src/ios/ios_xiso.c"), "-o", str(library)], check=True)
        cls.lib = C.CDLL(str(library))
        cls.lib.nfs_iso_open.argtypes = [C.POINTER(Iso), C.c_char_p, C.c_char_p, C.c_size_t]
        cls.lib.nfs_iso_close.argtypes = [C.POINTER(Iso)]
        cls.lib.nfs_iso_walk.argtypes = [C.POINTER(Iso), VISITOR, C.c_void_p, C.c_char_p, C.c_size_t]
        cls.lib.nfs_iso_read.argtypes = [C.POINTER(Iso), C.c_uint64, C.c_void_p, C.c_size_t]
    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()
    def fixture(self, directory=None, base=0, root_size=2048):
        path = self.folder / "disc.iso"
        descriptor = bytearray(2048)
        descriptor[:20] = b"MICROSOFT*XBOX*MEDIA"
        struct.pack_into("<II", descriptor, 0x14, 33, root_size)
        with path.open("wb") as f:
            f.seek(base + 32 * 2048); f.write(descriptor)
            f.seek(base + 33 * 2048); f.write((directory or entry("default.xbe")).ljust(2048, b"\xff"))
            f.seek(base + 34 * 2048); f.write(b"TEST" + b"\0" * 2044)
        return path
    def walk(self, path, cancel=False):
        iso = Iso(); error = C.create_string_buffer(256); found = []
        opened = self.lib.nfs_iso_open(C.byref(iso), str(path).encode(), error, len(error))
        if not opened: return False, found, error.value.decode()
        @VISITOR
        def visit(_, name, offset, size, directory):
            found.append((name.decode(), offset, size, directory))
            return not cancel
        try:
            ok = self.lib.nfs_iso_walk(C.byref(iso), visit, None, error, len(error))
            return bool(ok), found, error.value.decode()
        finally:
            self.lib.nfs_iso_close(C.byref(iso))
    def test_all_partition_layouts(self):
        for base in BASES:
            with self.subTest(base=base):
                ok, files, error = self.walk(self.fixture(base=base))
                self.assertTrue(ok, error)
                self.assertEqual(files, [("default.xbe", base + 34 * 2048, 4, 0)])
    def test_nested_directory(self):
        path = self.fixture(entry("NFSUNDER", 34, 2048, True))
        with path.open("r+b") as f:
            f.seek(34 * 2048); f.write(entry("data.bin", 35, 4).ljust(2048, b"\xff"))
            f.seek(35 * 2048); f.write(b"DATA")
        ok, files, error = self.walk(path)
        self.assertTrue(ok, error); self.assertEqual(files[1][0], "NFSUNDER/data.bin")
    def test_paths_cannot_escape_destination(self):
        for name in ("..", ".", "../evil", "foo\\evil", "foo:evil", "\x01bad"):
            with self.subTest(name=name):
                ok, _, error = self.walk(self.fixture(entry(name)))
                self.assertFalse(ok); self.assertIn("unsafe", error)
    def test_out_of_bounds_file(self):
        ok, _, error = self.walk(self.fixture(entry("data", 999, 100)))
        self.assertFalse(ok); self.assertIn("extent", error)
    def test_truncated_entry(self):
        data = bytearray(entry("data")); data[13] = 255
        ok, _, error = self.walk(self.fixture(bytes(data), root_size=16))
        self.assertFalse(ok); self.assertIn("Malformed", error)
    def test_cycle(self):
        ok, _, error = self.walk(self.fixture(entry("loop", 33, 2048, True)))
        self.assertFalse(ok); self.assertIn("cycle", error)
    def test_oversized_directory(self):
        ok, _, error = self.walk(self.fixture(root_size=0xffffffff))
        self.assertFalse(ok); self.assertIn("directory", error)
    def test_invalid_image(self):
        path = self.folder / "invalid.iso"; path.write_bytes(b"invalid")
        ok, _, error = self.walk(path)
        self.assertFalse(ok); self.assertIn("XDVDFS", error)
    def test_cancelled_visitor(self):
        ok, _, error = self.walk(self.fixture(), cancel=True)
        self.assertFalse(ok); self.assertIn("stopped", error)
    def test_read_bounds_and_exact_content(self):
        path = self.fixture(); iso = Iso(); error = C.create_string_buffer(256)
        self.assertTrue(self.lib.nfs_iso_open(C.byref(iso), str(path).encode(), error, len(error)))
        try:
            out = C.create_string_buffer(4)
            self.assertTrue(self.lib.nfs_iso_read(C.byref(iso), 34 * 2048, out, 4))
            self.assertEqual(out.raw, b"TEST")
            self.assertFalse(self.lib.nfs_iso_read(C.byref(iso), iso.size - 1, out, 4))
        finally: self.lib.nfs_iso_close(C.byref(iso))

if __name__ == "__main__": unittest.main(verbosity=2)
