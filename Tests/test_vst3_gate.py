"""Regression tests for fail-closed native release checks and host exit handling."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

repo = Path(__file__).parents[1]
script = repo / "Tools/vst3_gate.py"
if not script.exists():
    script = repo / "tools/scripts/vst3_gate.py"
spec = importlib.util.spec_from_file_location("vst3_gate", script)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


def elf():
    data = bytearray(128)
    data[:7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<HHI", data, 16, 3, 62, 1)
    struct.pack_into("<Q", data, 32, 64)
    struct.pack_into("<HHH", data, 52, 64, 56, 1)
    struct.pack_into("<IIQQQQQQ", data, 64, 1, 5, 0, 0, 0, 128, 128, 4096)
    return data


def pe():
    data = bytearray(2048)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 128)
    data[128:132] = b"PE\0\0"
    struct.pack_into("<HH", data, 132, 0x8664, 1)
    struct.pack_into("<HH", data, 148, 240, 0x2022)
    struct.pack_into("<H", data, 152, 0x20B)
    struct.pack_into("<II", data, 392 + 16, 512, 1024)
    struct.pack_into("<I", data, 392 + 36, 0x60000020)
    return data


class ReleaseGateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bundle = self.root / "Demo.vst3"
        self.binary = self.bundle / "Contents/x86_64-linux/Demo.so"
        self.binary.parent.mkdir(parents=True)
        self.binary.write_bytes(elf())
        self.metadata = self.bundle / "Contents/Resources/moduleinfo.json"
        self.metadata.parent.mkdir(parents=True)
        self.info = {"Name": "Demo", "Factory Info": {"Vendor": "Acme"}, "Classes": [
            {"CID": "1" * 32, "Name": "Demo", "Vendor": "Acme", "Category": "Audio Module Class"},
            {"CID": "2" * 32, "Name": "Demo", "Vendor": "Acme", "Category": "Component Controller Class"}]}
        self.save_metadata()

    def save_metadata(self):
        self.metadata.write_text(json.dumps(self.info), encoding="utf-8")

    def call(self, *extra):
        args = ["--bundle-dir", str(self.root), "--platform", "linux-x86_64", "--vendor", "Acme", "--product", "Demo"]
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return gate.main(args + list(extra))

    def test_structurally_valid_linux_and_windows_headers(self):
        self.assertEqual(gate.native_header(self.binary, "linux-x86_64")["architecture"], "x86_64")
        windows = self.root / "demo.dll"
        windows.write_bytes(pe())
        self.assertTrue(gate.native_header(windows, "windows-x64")["dll"])

    def test_empty_binary_fails_before_running_host(self):
        self.binary.write_bytes(b"")
        marker = self.root / "host-started"
        self.assertEqual(self.call("--", sys.executable, "-c", f"open({str(marker)!r},'w').close()"), 1)
        self.assertFalse(marker.exists())

    def test_wrong_architecture_and_out_of_bounds_segments(self):
        for offset, value in ((18, 3), (52, 0)):
            data = elf()
            struct.pack_into("<H", data, offset, value)
            self.binary.write_bytes(data)
            self.assertEqual(self.call(), 1)
        data = elf()
        struct.pack_into("<Q", data, 64 + 32, 1024)
        self.binary.write_bytes(data)
        self.assertEqual(self.call(), 1)

    def test_windows_non_dll_and_truncated_sections_fail(self):
        windows = self.root / "demo.dll"
        for offset, fmt, value in ((150, "<H", 0x22), (392 + 20, "<I", 2048), (152, "<H", 0x10B)):
            data = pe()
            struct.pack_into(fmt, data, offset, value)
            windows.write_bytes(data)
            with self.assertRaises(gate.GateError):
                gate.native_header(windows, "windows-x64")

    def test_missing_and_duplicate_bundles_fail(self):
        with self.assertRaises(gate.GateError):
            gate.discover(None, self.root, [], "Release", ["Demo", "Missing"])
        with self.assertRaises(gate.GateError):
            gate.discover(None, None, [self.bundle, self.bundle], "Release", ["Demo"])
        with self.assertRaises(gate.GateError):
            gate.discover(None, self.root / "absent", [], "Release", ["Demo"])

    def test_factory_class_vendor_and_identity_errors_fail(self):
        self.info["Factory Info"]["Vendor"] = "yourcompany"
        self.save_metadata()
        self.assertEqual(self.call(), 1)
        self.info["Factory Info"]["Vendor"] = "Acme"
        self.info["Classes"][1]["CID"] = self.info["Classes"][0]["CID"]
        self.save_metadata()
        self.assertEqual(self.call(), 1)
        self.info["Classes"][1]["CID"] = "2" * 32
        self.info["Classes"][1]["Vendor"] = "Wrong"
        self.save_metadata()
        self.assertEqual(self.call(), 1)

    def test_snapshot_detects_resource_or_class_identity_changes(self):
        snapshot = self.root / "before.json"
        self.assertEqual(self.call("--snapshot", str(snapshot)), 0)
        self.assertEqual(self.call("--check-snapshot", str(snapshot)), 0)
        self.info["Classes"][1]["CID"] = "3" * 32
        self.save_metadata()
        self.assertEqual(self.call("--check-snapshot", str(snapshot)), 1)

    def test_actual_command_repeats_and_records_exit(self):
        report = self.root / "report.json"
        self.assertEqual(self.call("--report", str(report), "--", sys.executable, "-c", "pass"), 0)
        result = json.loads(report.read_text())
        self.assertEqual(len(result["runs"]), 2)
        self.assertTrue(all(r["hostExit"] == 0 and r["filesAndIdentitiesUnchanged"] for r in result["runs"]))
        self.assertEqual(self.call("--report", str(report), "--", sys.executable, "-c", "raise SystemExit(7)"), 1)
        self.assertEqual(json.loads(report.read_text())["runs"][0]["hostExit"], 7)

    def test_successful_host_cannot_silently_truncate_binary(self):
        report = self.root / "report.json"
        self.assertEqual(self.call("--report", str(report), "--", sys.executable, "-c",
                                  f"open({str(self.binary)!r},'wb').close()"), 1)
        result = json.loads(report.read_text())
        self.assertFalse(result["passed"])
        self.assertEqual(result["runs"][0]["hostExit"], 0)

    def test_symlinked_resource_fails(self):
        resource = self.metadata.parent / "linked"
        try:
            resource.symlink_to(self.binary)
        except OSError:
            self.skipTest("symlink creation unavailable")
        self.assertEqual(self.call(), 1)


if __name__ == "__main__":
    unittest.main()
