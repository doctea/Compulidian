"""Build and stage the three release UF2 files without uploading anything."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys


ENVIRONMENTS = (
    "rpipico",
    "rpipico_16mb",
    "rpipico_16mb_with_cv_clock_input",
)


def find_platformio():
    for name in ("platformio", "pio"):
        executable = shutil.which(name)
        if executable:
            return executable

    for executable in (
        Path(sys.executable).with_name("platformio.exe"),
        Path(sys.executable).with_name("platformio"),
        Path.home() / ".platformio/penv/Scripts/platformio.exe",
        Path.home() / ".platformio/penv/bin/platformio",
    ):
        if executable.is_file():
            return str(executable)

    raise FileNotFoundError(
        "PlatformIO was not found. Install it or pass --pio PATH_TO_PLATFORMIO."
    )


def build_release(project_dir, platformio):
    command = [str(platformio), "run"]
    for environment in ENVIRONMENTS:
        command.extend(["--environment", environment])
    subprocess.run(command, cwd=project_dir, check=True)

    artifacts = [
        (
            project_dir / ".pio" / "build" / environment / f"firmware_{environment}.uf2",
            project_dir / "firmware" / f"firmware_{environment}.uf2",
        )
        for environment in ENVIRONMENTS
    ]
    for source, destination in artifacts:
        if not source.is_file():
            raise FileNotFoundError(f"Expected build output not found: {source}")

    (project_dir / "firmware").mkdir(exist_ok=True)
    for source, destination in artifacts:
        shutil.copy2(source, destination)
        print(f"Staged {destination}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pio", help="Path to the PlatformIO executable")
    args = parser.parse_args()
    try:
        build_release(
            Path(__file__).resolve().parents[1], args.pio or find_platformio()
        )
    except subprocess.CalledProcessError as error:
        print("Build failed; release files were not updated.", file=sys.stderr)
        return error.returncode
    except OSError as error:
        print(f"Release failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())