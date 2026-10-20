# Releasing

The current release is **v0.1.1**, with package version **0.1.1**. The original
v0.1 is a historical prerelease. Release metadata is recorded in [release.json](../release.json);
`scripts/check_release.py` checks it against the public header, CMake, Conan,
vcpkg and versioned release notes.

## Before tagging

1. Update `release.json`, `project(... VERSION ...)`, `discord::version`, the
   Conan recipe and vcpkg manifest together for the next release.
2. Write `release.md`, copy it to `docs/releases/<tag without v>.md`, and update
   the changelog. The release check requires both note files to match exactly.
3. Run `python scripts/check_release.py`. Locally, add `--secret-file test_token`
   to check the real test credentials without printing them.
4. Build and test static/shared configurations and installed-package consumers.
5. Commit, push `main`, and wait for both **build** and **packages** workflows.
6. Tag the tested commit and push the tag:

```bash
git tag -a v0.1.1 -m "discord.cpp-self v0.1.1"
git push origin v0.1.1
```

## What publish does

The [publish workflow](../.github/workflows/publish.yml) reruns the build and
package checks for the tag, downloads validated source archives, generates the
vcpkg overlay/MSYS2 recipe from the exact commit, collects tested native MSYS2
packages, and publishes a GitHub Release with notes,
downloadable `release.md` and SHA256 checksums. `release.json` selects whether
the release is a prerelease; normal releases are marked as latest.
It uses GitHub's built-in token; no personal access token
is needed. A failed build prevents publication.

The job can also be dispatched manually with an existing tag. It refuses a tag
that disagrees with `release.json` or points at another commit. It does not
overwrite an existing published release; an existing draft can be completed after
its replacement artifacts pass all checks. If only publication failed, fix the
repository setting and rerun the failed jobs. Do not move an already published
tag; release a new version for source corrections.

To reproduce source archives from a committed revision:

```bash
python scripts/package_source.py --ref v0.1.1
python scripts/check_release.py --archive build/dist/discord.cpp-self-v0.1.tar.gz
python scripts/make_vcpkg_overlay.py --ref v0.1.1
```

The overlay generator needs the commit to be available on GitHub. It calculates
the upstream archive's real SHA512, so there is no placeholder checksum in the
downloadable port. The generator reads its template and metadata from that commit.

Source packaging uses committed Git content, never the whole working directory.
Credentials, raw journals and build output are excluded and checked again in
both source archives. Dependency DLLs are not bundled: the MSYS2 packages declare
their runtime dependencies for pacman. Native packages are toolchain-specific.
Future binary releases must preserve LGPL notices and corresponding source.

See [publishing setup](PUBLISHING.md) for GitHub settings and Conan remote upload.
