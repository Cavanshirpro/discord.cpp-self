# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Cavanshirpro
# Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
"""Package committed source only, then validate each archive."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess

from check_release import ROOT, archive_files, validate


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ref", default="HEAD")
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "dist")
    args = parser.parse_args()
    commit = subprocess.run(["git", "rev-parse", "--verify", f"{args.ref}^{{commit}}"], cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip()
    metadata = json.loads(subprocess.run(["git", "show", f"{commit}:release.json"], cwd=ROOT, check=True, capture_output=True).stdout)
    prefix = f'discord.cpp-self-{metadata["tag"]}'
    args.output.mkdir(parents=True, exist_ok=True)
    zip_path = args.output / f"{prefix}.zip"
    tar_path = args.output / f"{prefix}.tar.gz"
    subprocess.run(["git", "archive", "--format=zip", f"--prefix={prefix}/", "-o", str(zip_path), commit], cwd=ROOT, check=True)
    with tar_path.open("wb") as output, gzip.GzipFile(filename="", fileobj=output, mode="wb", mtime=0) as compressed:
        process = subprocess.Popen(["git", "archive", "--format=tar", f"--prefix={prefix}/", commit], cwd=ROOT, stdout=subprocess.PIPE)
        try:
            while data := process.stdout.read(1024 * 1024):
                compressed.write(data)
        finally:
            process.stdout.close()
        if process.wait() != 0:
            raise RuntimeError("git archive failed")
    hashes = []
    for path in (zip_path, tar_path):
        files = archive_files(path)
        validate(files)
        hashes.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}")
        print(f"Validated {path.name}: {len(files)} files")
    notes_path = args.output / "release.md"
    notes_path.write_bytes(subprocess.run(["git", "show", f"{commit}:release.md"], cwd=ROOT, check=True, capture_output=True).stdout)
    hashes.append(f"{hashlib.sha256(notes_path.read_bytes()).hexdigest()}  {notes_path.name}")
    (args.output / "SHA256SUMS").write_text("\n".join(hashes) + "\n", encoding="utf-8")
    print(f"Source commit: {commit}")


if __name__ == "__main__":
    main()
