"""Create a self-contained QtGuiTestingMCP release archive.

The Qt executable and its deployed runtime are installed by CMake. This script
adds the MCP adapter, setup utility, documentation, and license files, then
creates a platform-appropriate archive for GitHub Releases.

Copyright (c) 2026 Chris Polewczuk
SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


PLATFORMS = {"windows": "zip", "linux": "gztar", "macos": "gztar"}
PROJECT_FILES = (
    "CHANGELOG.md",
    "CONTRIBUTING.md",
    "LICENSE",
    "README.md",
    "VERSION",
    "requirements.txt",
)
PROJECT_DIRECTORIES = ("docs", "scripts", "src")


def project_root() -> Path:
    """Return the repository root containing this script."""
    return Path(__file__).resolve().parents[1]


def read_version(root: Path) -> str:
    """Read and validate the release version from VERSION."""
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    if not version or any(character in version for character in "/\\"):
        raise ValueError("VERSION must contain a valid release identifier")
    return version


def add_project_files(root: Path, bundle: Path) -> None:
    """Copy the source-side files required to use the packaged bridge."""
    for relative_path in PROJECT_FILES:
        source = root / relative_path
        if not source.is_file():
            raise FileNotFoundError(source)
        shutil.copy2(source, bundle / relative_path)

    for relative_path in PROJECT_DIRECTORIES:
        source = root / relative_path
        destination = bundle / relative_path
        if not source.is_dir():
            raise FileNotFoundError(source)
        shutil.copytree(source, destination, dirs_exist_ok=True)


def create_archive(package_dir: Path, platform: str, version: str) -> Path:
    """Add project files and create the archive in the package directory's parent."""
    root = project_root()
    bundle_name = f"QtGuiTestingMCP-{version}-{platform}"
    bundle = package_dir.parent / bundle_name
    if bundle.exists():
        shutil.rmtree(bundle)
    package_dir.rename(bundle)
    add_project_files(root, bundle)

    output_directory = root / "dist"
    output_directory.mkdir(parents=True, exist_ok=True)
    archive_base = output_directory / bundle_name
    archive_path = Path(
        shutil.make_archive(
            str(archive_base),
            PLATFORMS[platform],
            root_dir=bundle.parent,
            base_dir=bundle.name,
        )
    )
    return archive_path


def main() -> int:
    """Parse arguments, package the CMake install tree, and report its archive."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--package-dir",
        type=Path,
        required=True,
        help="CMake install prefix containing the deployed executable",
    )
    parser.add_argument("--platform", choices=sorted(PLATFORMS), required=True)
    arguments = parser.parse_args()

    root = project_root()
    version = read_version(root)
    package_dir = arguments.package_dir.resolve()
    if not package_dir.is_dir():
        parser.error(f"package directory does not exist: {package_dir}")

    archive = create_archive(package_dir, arguments.platform, version)
    print(archive)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
