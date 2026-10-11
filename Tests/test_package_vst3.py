"""MixAgent release packaging completeness, identity and integrity regressions."""
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

from test_vst3_gate import elf, pe

script = Path(__file__).parents[1] / "Tools/package_release.py"
sys.path.insert(0, str(script.parent))
spec = importlib.util.spec_from_file_location("package_release", script)
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.out = self.root / "release"
        self.base = self.build / "MixAgent_artefacts/Release"

    def fixture(self, platform="windows-x64"):
        bundle = self.base / "VST3/MixAgent.vst3"
        arch, suffix = ("x86_64-win", ".vst3") if platform == "windows-x64" else ("x86_64-linux", ".so")
        binary = bundle / "Contents" / arch / ("MixAgent" + suffix)
        binary.parent.mkdir(parents=True)
        binary.write_bytes(pe() if platform == "windows-x64" else elf())
        resource = bundle / "Contents/Resources/moduleinfo.json"
        resource.parent.mkdir()
        resource.write_text(json.dumps({"Name": "MixAgent", "Factory Info": {"Vendor": "AgentMix"}, "Classes": [
            {"CID": "1" * 32, "Name": "MixAgent", "Vendor": "AgentMix", "Category": "Audio Module Class"},
            {"CID": "2" * 32, "Name": "MixAgent", "Vendor": "AgentMix", "Category": "Component Controller Class"}]}))
        standalone = self.base / "Standalone" / ("MixAgent.exe" if platform == "windows-x64" else "MixAgent")
        standalone.parent.mkdir()
        data = pe() if platform == "windows-x64" else elf()
        if platform == "windows-x64":
            struct.pack_into("<H", data, 150, 0x22)
        standalone.write_bytes(data)
        (self.build / "mixagent-ui.png").write_bytes(b"\x89PNG\r\n\x1a\nfixture")
        return binary, standalone

    def package(self, platform="windows-x64"):
        return release.package(self.build, self.out, "Release", platform, "a" * 40)

    def test_complete_release_preserves_bundle_and_provenance_on_both_platforms(self):
        for platform in ("windows-x64", "linux-x86_64"):
            self.fixture(platform)
            manifest = self.package(platform)
            self.assertEqual(release.verify(self.out), manifest)
            self.assertEqual(len(manifest["files"]), 4)
            self.assertEqual(manifest["sourceCommit"], "a" * 40)
            self.assertNotIn("testsPassed", manifest)
            import shutil
            shutil.rmtree(self.out)
            shutil.rmtree(self.build)

    def test_zero_or_missing_deliverables_do_not_expose_output(self):
        for missing in ("plugin", "standalone", "image"):
            binary, standalone = self.fixture()
            {"plugin": binary, "standalone": standalone, "image": self.build / "mixagent-ui.png"}[missing].write_bytes(b"")
            with self.assertRaises((release.GateError, OSError)):
                self.package()
            self.assertFalse(self.out.exists())
            import shutil
            shutil.rmtree(self.build)

    def test_missing_bundle_fails(self):
        self.build.mkdir()
        with self.assertRaises(release.GateError):
            self.package()
        self.assertFalse(self.out.exists())

    def test_corruption_or_added_files_fail_verification(self):
        self.fixture()
        manifest = self.package()
        target = self.out / manifest["files"][0]["path"]
        original = target.read_bytes()
        target.write_bytes(original + b"bad")
        with self.assertRaises(release.GateError):
            release.verify(self.out)
        target.write_bytes(original)
        (self.out / "untracked").write_text("unexpected")
        with self.assertRaises(release.GateError):
            release.verify(self.out)

    def test_provenance_must_pin_full_source_and_juce(self):
        self.fixture()
        with self.assertRaises(release.GateError):
            release.package(self.build, self.out, "Release", "windows-x64", "abcdef", release.JUCE_COMMIT)
        with self.assertRaises(release.GateError):
            release.package(self.build, self.out, "Release", "windows-x64", "a" * 40, "b" * 40)
        self.package()
        self.assertEqual(release.verify(self.out, "a" * 40)["sourceCommit"], "a" * 40)
        with self.assertRaisesRegex(release.GateError, "expected source"):
            release.verify(self.out, "b" * 40)

    def test_existing_release_is_preserved(self):
        self.fixture()
        self.package()
        before = (self.out / "build-manifest.json").read_bytes()
        with self.assertRaises(release.GateError):
            self.package()
        self.assertEqual((self.out / "build-manifest.json").read_bytes(), before)

    def test_architecture_manifest_and_checksum_tampering_fail(self):
        self.fixture()
        self.package()
        path = self.out / "build-manifest.json"
        original = path.read_text()
        manifest = json.loads(original)
        manifest["nativeIdentity"]["products"][0]["header"]["architecture"] = "aarch64"
        path.write_text(json.dumps(manifest))
        with self.assertRaises(release.GateError):
            release.verify(self.out)
        path.write_text(original)
        (self.out / "checksums.sha256").write_text("wrong")
        with self.assertRaises(release.GateError):
            release.verify(self.out)


if __name__ == "__main__":
    unittest.main()
