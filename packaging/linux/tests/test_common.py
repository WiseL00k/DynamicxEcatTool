import contextlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common


class PackagingFailureTests(unittest.TestCase):
    def test_version_rejects_paths_prefixes_and_malformed_numbers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "packaging").mkdir()
            with patch.object(common, "ROOT", root):
                for invalid in ("v0.4.0", "../0.4.0", "0.4", "01.4.0", "0.4.0\n1.0.0"):
                    (root / "packaging/VERSION").write_text(invalid)
                    with self.subTest(version=invalid), self.assertRaises(ValueError):
                        common.version()
                (root / "packaging/VERSION").write_text("0.4.0\n")
                self.assertEqual(common.version(), "0.4.0")

    def test_cached_file_must_match_pinned_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tool"
            path.write_bytes(b"original")
            spec = {"url": "https://example.invalid/tool", "sha256": common.sha256(path)}
            with patch("urllib.request.urlopen", side_effect=AssertionError("Must not download")):
                self.assertEqual(common.download(spec, path), path)
                path.write_bytes(b"corrupted")
                with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
                    common.download(spec, path)

    def test_invalid_download_is_never_saved_as_verified_tool(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tool"
            spec = {"url": "https://example.invalid/tool", "sha256": "0" * 64}
            with patch("urllib.request.urlopen", return_value=contextlib.closing(io.BytesIO(b"wrong"))):
                with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
                    common.download(spec, path)
            self.assertFalse(path.exists())
            self.assertFalse(path.with_suffix(".partial").exists())

    def test_insecure_or_unpinned_download_is_rejected(self):
        for url, digest in (("http://example.invalid/tool", "0" * 64), ("https://example.invalid/tool", "")):
            with self.subTest(url=url, digest=digest), self.assertRaises(ValueError):
                common.download({"url": url, "sha256": digest}, Path("unused"))

    def test_external_command_failure_propagates(self):
        with self.assertRaises(subprocess.CalledProcessError):
            common.run(sys.executable, "-c", "raise SystemExit(7)")


if __name__ == "__main__":
    unittest.main()
