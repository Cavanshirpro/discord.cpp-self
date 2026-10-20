# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Cavanshirpro
# Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
"""Validate publication contents without printing matched credentials."""
import argparse
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TOKEN = re.compile(rb"(?:mfa\.[A-Za-z0-9_-]{20,}|[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{5,}\.[A-Za-z0-9_-]{20,}|gh[pousr]_[A-Za-z0-9]{30,})")
FORBIDDEN = {".git", "build", "dist", ".cache", "__pycache__", ".env"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def repository_files():
    result = subprocess.run(["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=ROOT, check=True, capture_output=True)
    return {name: (ROOT / name).read_bytes() for name in sorted(set(result.stdout.decode().split("\0")) - {""})}


def archive_files(path):
    entries = {}
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            for info in archive.infolist():
                if not info.is_dir():
                    require((info.external_attr >> 16) & 0o170000 != 0o120000, "Archive contains a symlink")
                    entries[info.filename] = archive.read(info)
    else:
        with tarfile.open(path, "r:gz") as archive:
            for item in archive.getmembers():
                require(item.isfile() or item.isdir(), "Archive contains a non-regular entry")
                if item.isfile():
                    entries[item.name] = archive.extractfile(item).read()
    prefixes = {PurePosixPath(name).parts[0] for name in entries}
    require(len(prefixes) == 1, "Expected one source archive root")
    for name in entries:
        parts = PurePosixPath(name).parts
        require(not name.startswith("/") and ".." not in parts, "Unsafe archive path")
    return {"/".join(PurePosixPath(name).parts[1:]): data for name, data in entries.items()}


def validate(files, secrets=()):
    required = {"CMakeLists.txt", "README.md", "LICENSE", "COPYING", "NOTICE", "THIRD_PARTY.md", "CHANGELOG.md", "SECURITY.md", "CONTRIBUTING.md", "docs/API.md", "docs/COMPARISON.md", "tests/unit.cpp", "tests/loopback.py"}
    require(required <= files.keys(), "Required publication files are missing")
    require(b"GNU LESSER GENERAL PUBLIC LICENSE" in files["LICENSE"], "Missing LGPL text")
    require(b"GNU GENERAL PUBLIC LICENSE" in files["COPYING"], "Missing incorporated GPL text")
    require(b"LGPL-3.0-or-later" in files["NOTICE"], "Incorrect project license notice")
    version = re.search(rb"project\(discord_cpp_self VERSION ([0-9.]+)", files["CMakeLists.txt"]).group(1).decode()
    require(f'"{version}"'.encode() in files["include/discord/discord.hpp"], "Public version does not match CMake")
    metadata = json.loads(files["release.json"])
    require(metadata["version"] == version, "Release metadata does not match CMake")
    require(re.fullmatch(r"v[0-9]+\.[0-9]+(?:\.[0-9]+)?", metadata["tag"]), "Invalid release tag")
    require(tuple(map(int, metadata["tag"][1:].split("."))) + ((0,) if metadata["tag"].count(".") == 1 else ()) == tuple(map(int, version.split("."))), "Release tag does not match package version")
    require(f'docs/releases/{metadata["tag"][1:]}.md' in files, "Missing versioned release notes")
    require(f'version = "{version}"'.encode() in files["conanfile.py"], "Conan version does not match CMake")
    require(json.loads(files["packaging/vcpkg/vcpkg.json"])["version-semver"] == version, "vcpkg version does not match CMake")
    for name, data in files.items():
        parts = PurePosixPath(name).parts
        require(not any(part in FORBIDDEN for part in parts), f"Private/generated path in publication: {name}")
        require(not any(part.startswith("test_token") or (part.startswith(".env.") and part != ".env.example") for part in parts), f"Credential path in publication: {name}")
        require(not TOKEN.search(data) and not any(secret and secret in data for secret in secrets), f"Credential match in {name}; value withheld")
        text = data.decode("utf-8")
        if name.endswith((".md", ".cpp", ".hpp", ".py", ".ps1", ".yml")):
            require(not re.search("[\u0131\u0130\u011f\u011e\u015f\u015e\u00e7\u00c7\u00f6\u00d6\u00fc\u00dc]", text), f"Non-English project text in {name}")
        if name.endswith((".cpp", ".hpp", ".py", ".ps1")):
            require("SPDX-License-Identifier: LGPL-3.0-or-later" in text, f"Missing SPDX notice in {name}")
            require("Copyright (C) 2026 Cavanshirpro" in text and "see NOTICE, LICENSE and COPYING" in text, f"Missing completed copyright notice in {name}")
        if name.endswith(".md"):
            prose = re.sub(r"(?ms)^```.*?^```[^\n]*(?:\n|$)", "", text)
            prose = re.sub(r"`[^`\n]*`", "", prose)
            for link in re.findall(r"!?\[[^\]]*\]\(([^\s)]+)(?:\s+[^)]*)?\)", prose):
                if "://" in link or link.startswith(("#", "mailto:")):
                    continue
                target = link.split("#", 1)[0].strip("<>")
                if not target:
                    continue
                from posixpath import normpath, join, dirname
                require(normpath(join(dirname(name), target)) in files, f"Broken local link in {name}: {target}")
    return version


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--secret-file", type=Path)
    args = parser.parse_args()
    secrets = TOKEN.findall(args.secret_file.read_bytes()) if args.secret_file else ()
    files = archive_files(args.archive) if args.archive else repository_files()
    version = validate(files, secrets)
    print(f"Release {version}: {len(files)} files checked; links, license notices and credential scans passed")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Release check failed: {error}", file=sys.stderr)
        sys.exit(1)
