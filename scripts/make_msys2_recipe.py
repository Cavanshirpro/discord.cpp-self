# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Cavanshirpro
# Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
"""Generate a source-building MSYS2 recipe with a real commit and source SHA512."""
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
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "msys2")
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "rev-parse", f"{args.ref}^{{commit}}"], cwd=ROOT).decode().strip()
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Expected a full Git commit")

    def committed(name):
        return subprocess.check_output(["git", "show", f"{commit}:{name}"], cwd=ROOT)

    metadata = json.loads(committed("release.json"))
    request = urllib.request.Request(f"https://github.com/Cavanshirpro/discord.cpp-self/archive/{commit}.tar.gz", headers={"User-Agent": "discord.cpp-self-release"})
    with urllib.request.urlopen(request, timeout=120) as response:
        data = response.read()
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        if archive.extractfile(f"discord.cpp-self-{commit}/release.json").read() != committed("release.json"):
            raise ValueError("Downloaded source differs from the requested release metadata")
    recipe = committed("packaging/msys2/PKGBUILD.in").decode()
    for key, value in {"SOURCE_COMMIT": commit, "PACKAGE_VERSION": metadata["version"], "SOURCE_SHA512": hashlib.sha512(data).hexdigest()}.items():
        recipe = recipe.replace(f"@{key}@", value)
    folder = args.output / "mingw-w64-discord-cpp-self"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "PKGBUILD").write_text(recipe, encoding="utf-8", newline="\n")
    (folder / "README.md").write_bytes(committed("packaging/msys2/README.md"))
    target = ROOT / "build" / "dist" / f'discord.cpp-self-{metadata["tag"]}-msys2-recipe.zip'
    target.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(folder.iterdir()):
            info = zipfile.ZipInfo(f"msys2/mingw-w64-discord-cpp-self/{path.name}")
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, path.read_bytes())
    print(f"Generated {target.name} for {commit}")


if __name__ == "__main__":
    main()
