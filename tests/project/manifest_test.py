#!/usr/bin/env python3
"""Run the private tc.project/root-discovery unit executable."""

import argparse
import os
from pathlib import Path
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="run the unit checks")
    parser.add_argument(
        "executable",
        nargs="?",
        default=os.environ.get(
            "TC_PROJECT_MANIFEST_TEST", "build/tests/project_manifest_test"
        ),
    )
    args = parser.parse_args()
    executable = Path(args.executable).resolve()
    if not executable.is_file() or not os.access(executable, os.X_OK):
        print(f"project manifest test executable not found: {executable}", file=sys.stderr)
        return 70
    try:
        result = subprocess.run([str(executable)], check=False, timeout=60)
    except subprocess.TimeoutExpired:
        print("project manifest tests timed out", file=sys.stderr)
        return 71
    except OSError as error:
        print(f"cannot run project manifest tests: {error}", file=sys.stderr)
        return 70
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
