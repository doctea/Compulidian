"""Run with python3 -m unittest discover -s test -p test_build_release.py."""

from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools import build_release


class BuildReleaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.project = Path(temporary.name)
        self.outputs = {}
        self.releases = {}
        (self.project / "firmware").mkdir()
        for environment in build_release.ENVIRONMENTS:
            output = self.project / ".pio/build" / environment / f"firmware_{environment}.uf2"
            output.parent.mkdir(parents=True)
            output.write_bytes(environment.encode("ascii"))
            self.outputs[environment] = output
            release = self.project / "firmware" / f"firmware_{environment}.uf2"
            release.write_bytes(b"previous release")
            self.releases[environment] = release

    def assert_releases_unchanged(self):
        for release in self.releases.values():
            self.assertEqual(release.read_bytes(), b"previous release")

    def test_builds_exactly_the_release_environments_and_copies_outputs(self):
        with patch.object(build_release.subprocess, "run") as run:
            build_release.build_release(self.project, "platformio")
        run.assert_called_once_with(
            [
                "platformio", "run",
                "--environment", "rpipico",
                "--environment", "rpipico_16mb",
                "--environment", "rpipico_16mb_with_cv_clock_input",
            ],
            cwd=self.project,
            check=True,
        )
        for environment, release in self.releases.items():
            self.assertEqual(release.read_bytes(), environment.encode("ascii"))

    def test_failed_build_preserves_all_existing_releases(self):
        with patch.object(
            build_release.subprocess, "run",
            side_effect=subprocess.CalledProcessError(1, "platformio"),
        ):
            with self.assertRaises(subprocess.CalledProcessError):
                build_release.build_release(self.project, "platformio")
        self.assert_releases_unchanged()

    def test_missing_last_output_preserves_all_existing_releases(self):
        self.outputs[build_release.ENVIRONMENTS[-1]].unlink()
        with patch.object(build_release.subprocess, "run"):
            with self.assertRaises(FileNotFoundError):
                build_release.build_release(self.project, "platformio")
        self.assert_releases_unchanged()

    def test_creates_release_directory_when_missing(self):
        for release in self.releases.values():
            release.unlink()
        (self.project / "firmware").rmdir()
        with patch.object(build_release.subprocess, "run"):
            build_release.build_release(self.project, "platformio")
        self.assertTrue(all(release.is_file() for release in self.releases.values()))

    def test_finds_platformio_on_path(self):
        with patch.object(build_release.shutil, "which", return_value="pio"):
            self.assertEqual(build_release.find_platformio(), "pio")

    def test_reports_missing_platformio(self):
        with patch.object(build_release.shutil, "which", return_value=None):
            with patch.object(Path, "is_file", return_value=False):
                with self.assertRaisesRegex(FileNotFoundError, "--pio"):
                    build_release.find_platformio()


if __name__ == "__main__":
    unittest.main()