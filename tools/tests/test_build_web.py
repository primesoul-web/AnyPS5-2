import hashlib
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_web
import package_release

FIRST = "a1" * 20
SECOND = "b2" * 20
REPOSITORY = "owner/AnyPS5"


class SiteFixture(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.web = self.root / "build/web"
        (self.web / "native").mkdir(parents=True)
        (self.web / "native/relinker_wasm.mjs").write_bytes(b"converter module")
        (self.web / "native/relinker_wasm.wasm").write_bytes(b"converter binary")
        for name in build_web.FRONTEND:
            (self.web / name).write_text(f"built {name}", encoding="utf-8")
        (self.web / "index.html").write_text('<link href="app.css"><script src="app.mjs"></script>', encoding="utf-8")
        self.dist = self.root / "dist"
        self.dist.mkdir()

    def archive(self, platform, commit, entries=None):
        path = self.dist / f"prx-{platform}-vdev-{commit}.zip"
        with zipfile.ZipFile(path, "w") as archive:
            for name, content in (entries or {"libs/libc.prx": f"{platform} {commit}".encode()}).items():
                archive.writestr(name, content)
        return path

    def publish(self, commit):
        build_web.publish_snapshot(self.web, self.dist, f"vdev-{commit}", commit, REPOSITORY)

    def manifest(self):
        return json.loads((self.web / "release.json").read_text(encoding="utf-8"))

    def snapshot(self):
        return {path.relative_to(self.web).as_posix(): path.read_bytes() for path in self.web.rglob("*") if path.is_file()}


class SnapshotSiteTests(SiteFixture):
    def test_local_archives_build_a_complete_same_commit_site_and_web_zip(self):
        for platform in build_web.PLATFORMS:
            self.archive(platform, FIRST)
        self.publish(FIRST)
        manifest = self.manifest()
        self.assertEqual(manifest["tag"], f"vdev-{FIRST}")
        self.assertEqual(manifest["commit"], FIRST)
        self.assertEqual(manifest["url"], f"https://github.com/{REPOSITORY}/releases/tag/vdev-{FIRST}")
        self.assertEqual(manifest["converter"], {"path": f"native/{FIRST}/relinker_wasm.mjs"})
        self.assertEqual((self.web / f"native/{FIRST}/relinker_wasm.mjs").read_bytes(), b"converter module")
        self.assertEqual((self.web / f"native/{FIRST}/relinker_wasm.wasm").read_bytes(), b"converter binary")
        self.assertEqual((self.web / "native/relinker_wasm.mjs").read_bytes(), b"converter module")
        for platform in build_web.PLATFORMS:
            source = (self.dist / f"prx-{platform}-vdev-{FIRST}.zip").read_bytes()
            checksum = hashlib.sha256(source).hexdigest()
            entry = manifest["platforms"][platform]
            self.assertEqual(entry["path"], f"libraries/{platform}-{checksum}.zip")
            self.assertEqual(entry["size"], len(source))
            self.assertEqual(entry["sha256"], checksum)
            self.assertEqual(entry["expandedBytes"], len(f"{platform} {FIRST}"))
            self.assertEqual((self.web / entry["path"]).read_bytes(), source)
        (self.web / "progress.json").write_text("{}", encoding="utf-8")
        package_release.package_web(self.web, self.dist, f"vdev-{FIRST}")
        with zipfile.ZipFile(self.dist / f"web-vdev-{FIRST}.zip") as archive:
            contents = {name: archive.read(name) for name in archive.namelist()}
        self.assertEqual(contents, self.snapshot())
        self.assertIn("progress.json", contents)
        self.assertIn(f"native/{FIRST}/relinker_wasm.wasm", contents)

    def test_new_snapshot_replaces_previous_commit_assets(self):
        for platform in build_web.PLATFORMS:
            self.archive(platform, FIRST)
            self.archive(platform, SECOND)
        self.publish(FIRST)
        self.publish(SECOND)
        manifest = self.manifest()
        self.assertEqual(manifest["commit"], SECOND)
        self.assertFalse((self.web / "native" / FIRST).exists())
        self.assertEqual(sorted(path.name for path in (self.web / "libraries").iterdir()), sorted(entry["path"].removeprefix("libraries/") for entry in manifest["platforms"].values()))

    def test_invalid_second_platform_preserves_previous_site(self):
        for platform in build_web.PLATFORMS:
            self.archive(platform, FIRST)
        self.publish(FIRST)
        previous = self.snapshot()
        self.archive("windows", SECOND)
        cases = {
            "missing": None,
            "layout": {"libc.prx": b"outside libs"},
            "traversal": {"libs/../libc.prx": b"escape"},
            "no modules": {"libs/readme.txt": b"text"},
        }
        for case, entries in cases.items():
            with self.subTest(case):
                (self.dist / f"prx-linux-vdev-{SECOND}.zip").unlink(missing_ok=True)
                if entries is not None:
                    self.archive("linux", SECOND, entries)
                with self.assertRaises(ValueError):
                    self.publish(SECOND)
                self.assertEqual(self.snapshot(), previous)
                self.assertEqual(sorted(path.name for path in self.web.iterdir() if path.name.startswith(".libraries-")), [])
        (self.dist / f"prx-linux-vdev-{SECOND}.zip").write_bytes(b"truncated archive")
        with self.assertRaises(ValueError):
            self.publish(SECOND)
        self.assertEqual(self.snapshot(), previous)

    def test_version_and_commit_must_match(self):
        for platform in build_web.PLATFORMS:
            self.archive(platform, FIRST)
        for version, commit in ((f"vdev-{SECOND}", FIRST), (f"vdev-{FIRST}", FIRST.upper()), ("v1.0", FIRST), (f"vdev-{FIRST[:7]}", FIRST[:7])):
            with self.subTest(version=version, commit=commit), self.assertRaises(ValueError):
                build_web.publish_snapshot(self.web, self.dist, version, commit, REPOSITORY)
        with self.assertRaises(ValueError):
            build_web.publish_snapshot(self.web, self.dist, f"vdev-{FIRST}", FIRST, "owner/..")
        self.assertFalse((self.web / "release.json").exists())
        self.assertFalse((self.web / "libraries").exists())

    def test_missing_built_frontend_is_not_replaced_from_sources(self):
        for platform in build_web.PLATFORMS:
            self.archive(platform, FIRST)
        (self.web / "worker.mjs").unlink()
        with self.assertRaisesRegex(ValueError, "worker.mjs"):
            self.publish(FIRST)
        self.assertFalse((self.web / "worker.mjs").exists())
        self.assertFalse((self.web / "release.json").exists())

    def test_partial_snapshot_arguments_are_rejected(self):
        with patch.object(sys, "argv", ["build_web.py", "--build", str(self.web.parent), "--version", f"vdev-{FIRST}"]), patch("sys.stderr"), self.assertRaises(SystemExit):
            build_web.main()
        self.assertFalse((self.web / ".nojekyll").exists())


class WebPackageTests(SiteFixture):
    def setUp(self):
        super().setUp()
        for platform in build_web.PLATFORMS:
            self.archive(platform, FIRST)
        self.publish(FIRST)
        self.output = self.root / "release"
        self.version = f"vdev-{FIRST}"

    def rewrite(self, change):
        manifest = json.loads((self.web / "release.json").read_text(encoding="utf-8"))
        change(manifest)
        (self.web / "release.json").write_text(json.dumps(manifest), encoding="utf-8")

    def assertRejected(self, version=None):
        with self.assertRaises((RuntimeError, ValueError)):
            package_release.package_web(self.web, self.output, version or self.version)
        self.assertFalse((self.output / f"web-{version or self.version}.zip").exists())

    def test_tag_without_matching_manifest_is_rejected(self):
        self.assertRejected(f"vdev-{SECOND}")

    def test_manifest_commit_must_match_snapshot_tag(self):
        self.rewrite(lambda manifest: manifest.update(commit=SECOND))
        self.assertRejected()

    def test_converter_must_be_scoped_to_the_manifest_commit(self):
        self.rewrite(lambda manifest: manifest.update(converter={"path": "native/relinker_wasm.mjs"}))
        self.assertRejected()

    def test_commit_scoped_converter_must_match_built_converter(self):
        (self.web / f"native/{FIRST}/relinker_wasm.wasm").write_bytes(b"other converter")
        self.assertRejected()

    def test_tampered_library_is_rejected(self):
        entry = json.loads((self.web / "release.json").read_text(encoding="utf-8"))["platforms"]["linux"]
        library = self.web / entry["path"]
        contents = bytearray(library.read_bytes())
        contents[-1] ^= 1
        library.write_bytes(contents)
        self.assertRejected()

    def test_missing_wasm_is_rejected(self):
        (self.web / "native/relinker_wasm.wasm").unlink()
        self.assertRejected()

    def test_symbolic_links_are_rejected(self):
        try:
            (self.web / "linked.html").symlink_to(self.web / "index.html")
        except (OSError, NotImplementedError):
            self.skipTest("Symbolic links are unavailable")
        self.assertRejected()

    def test_worker_from_a_different_revision_cannot_be_published(self):
        (self.web / f"native/{FIRST}/worker.mjs").write_bytes(b"other worker protocol")
        self.assertRejected()



if __name__ == "__main__":
    unittest.main()
