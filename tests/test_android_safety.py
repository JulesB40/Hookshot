"""Synthetic fixtures only. Check original data preservation and path isolation."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare_lift", ROOT / "android/prepare_lift.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class AndroidSafetyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "original/exefs"
        self.source.mkdir(parents=True)
        self.outputs = self.root / "isolated"
        self.outputs.mkdir()
        for name in ("main", "sdk", "main.npdm"):
            (self.source / name).write_bytes(b"authored fixture: " + name.encode())
        (self.source.parent / "save.dat").write_bytes(b"original save sentinel")

    def test_copies_are_independent_and_originals_unchanged(self):
        before = {p: (p.read_bytes(), p.stat().st_ino, p.stat().st_mtime_ns) for p in self.source.parent.rglob("*") if p.is_file()}
        output = prepare.snapshot_exefs(self.source, self.outputs)
        hashes = json.loads((output / "original-sha256.json").read_text())
        for name in ("main", "sdk", "main.npdm"):
            copied, source = output / "exefs" / name, self.source / name
            self.assertNotEqual(copied.stat().st_ino, source.stat().st_ino)
            self.assertEqual(hashes[name], hashlib.sha256(source.read_bytes()).hexdigest())
            copied.write_bytes(b"simulated in-place unpacking on copy")
        for p, state in before.items():
            self.assertEqual((p.read_bytes(), p.stat().st_ino, p.stat().st_mtime_ns), state)
        self.assertFalse((output / "save.dat").exists())

    def test_repeat_does_not_replace_existing_build_or_saves(self):
        first = prepare.snapshot_exefs(self.source, self.outputs)
        (first / "sentinel").write_bytes(b"existing build and saves")
        second = prepare.snapshot_exefs(self.source, self.outputs)
        self.assertNotEqual(first, second)
        self.assertEqual((first / "sentinel").read_bytes(), b"existing build and saves")

    def test_output_inside_original_is_refused_before_any_write(self):
        for parent in (self.source, self.source.parent, self.root):
            with self.subTest(parent=parent), self.assertRaises(ValueError):
                prepare.snapshot_exefs(self.source, parent)

    def test_symlink_into_original_is_refused(self):
        alias = self.root / "alias"
        alias.symlink_to(self.source, target_is_directory=True)
        with self.assertRaises(ValueError):
            prepare.snapshot_exefs(self.source, alias)

    def test_module_symlink_is_refused_without_following_it(self):
        (self.source / "subsdk0").symlink_to(self.source.parent / "save.dat")
        with self.assertRaises(OSError):
            prepare.snapshot_exefs(self.source, self.outputs)
        self.assertEqual((self.source.parent / "save.dat").read_bytes(), b"original save sentinel")

    def test_special_file_is_refused_without_blocking(self):
        os.mkfifo(self.source / "subsdk0")
        with self.assertRaises(ValueError):
            prepare.snapshot_exefs(self.source, self.outputs)

    def test_permissions_are_not_changed(self):
        (self.source / "main").chmod(0o400)
        prepare.snapshot_exefs(self.source, self.outputs)
        self.assertEqual((self.source / "main").stat().st_mode & 0o777, 0o400)

    def test_apk_has_no_permissions_and_uses_distinct_package(self):
        manifest = ET.parse(ROOT / "android/AndroidManifest.xml").getroot()
        self.assertEqual(manifest.attrib["package"], "dev.julesb40.hookshot.experimental")
        self.assertFalse(manifest.findall("uses-permission"))
        android = "{http://schemas.android.com/apk/res/android}"
        self.assertEqual(manifest.find("application").attrib[android + "allowBackup"], "false")


if __name__ == "__main__":
    unittest.main()
