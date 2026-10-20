# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Cavanshirpro
# Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
"""Generate a commit-pinned overlay with the real upstream archive checksum."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ref", default="HEAD")
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "vcpkg-overlay")
    args = parser.parse_args()
    commit = subprocess.run(["git", "rev-list", "--max-count=1", args.ref, "--"], cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip()
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Expected a full Git commit")
    def committed(name):
        return subprocess.run(["git", "show", f"{commit}:{name}"], cwd=ROOT, check=True, capture_output=True).stdout
    url = f"https://github.com/Cavanshirpro/discord.cpp-self/archive/{commit}.tar.gz"
    request = urllib.request.Request(url, headers={"User-Agent": "discord.cpp-self-release"})
    with urllib.request.urlopen(request, timeout=120) as response:
        data = response.read()
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        root = f"discord.cpp-self-{commit}"
        if archive.extractfile(f"{root}/release.json").read() != committed("release.json"):
            raise ValueError("Downloaded source metadata differs from the requested commit")
    port = args.output / "discord-cpp-self"
    port.mkdir(parents=True, exist_ok=True)
    template = committed("packaging/vcpkg/portfile.cmake.in").decode()
    (port / "portfile.cmake").write_text(template.replace("@SOURCE_COMMIT@", commit).replace("@SOURCE_SHA512@", hashlib.sha512(data).hexdigest()), encoding="utf-8", newline="\n")
    (port / "vcpkg.json").write_bytes(committed("packaging/vcpkg/vcpkg.json"))
    metadata = json.loads(committed("release.json"))
    dist = ROOT / "build" / "dist"
    dist.mkdir(parents=True, exist_ok=True)
    target = dist / f'discord.cpp-self-{metadata["tag"]}-vcpkg-overlay.zip'
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(port.iterdir()):
            info = zipfile.ZipInfo(f"vcpkg-overlay/discord-cpp-self/{path.name}")
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, path.read_bytes())
    print(f"Generated {target.name} for {commit}")


if __name__ == "__main__":
    main()
