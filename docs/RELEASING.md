# Releasing

The first public release is **v0.1**, an early prerelease. Package managers and
CMake use **0.1.0**. The mapping is recorded in [release.json](../release.json);
`scripts/check_release.py` checks it against the public header, CMake, Conan,
vcpkg and versioned release notes.

## Before tagging

1. Update `release.json`, `project(... VERSION ...)`, `discord::version`, the
   Conan recipe and vcpkg manifest together for the next release.
2. Write `docs/releases/<tag without v>.md` and update the changelog.
3. Run `python scripts/check_release.py`. Locally, add `--secret-file test_token`
   to check the real test credentials without printing them.
4. Build and test static/shared configurations and installed-package consumers.
5. Commit, push `main`, and wait for both **build** and **packages** workflows.
6. Tag the tested commit and push the tag:

```bash
git tag -a v0.1 -m "discord.cpp-self v0.1"
git push origin v0.1
```

## What publish does

The [publish workflow](../.github/workflows/publish.yml) reruns the build and
package checks for the tag, downloads validated source archives, generates the
vcpkg overlay from the exact commit, and publishes a GitHub prerelease with notes
and SHA256 checksums. It uses GitHub's built-in token; no personal access token
is needed. A failed build prevents publication.

The job can also be dispatched manually with an existing tag. It refuses a tag
that disagrees with `release.json` or points at another commit. It does not
overwrite an existing published release. If only publication failed, fix the
repository setting and rerun the failed jobs. Do not move an already published
tag; release a new version for source corrections.

To reproduce source archives from a committed revision:

```bash
python scripts/package_source.py --ref v0.1
python scripts/check_release.py --archive build/dist/discord.cpp-self-v0.1.tar.gz
python scripts/make_vcpkg_overlay.py --ref v0.1
```

The overlay generator needs the commit to be available on GitHub. It calculates
the upstream archive's real SHA512, so there is no placeholder checksum in the
downloadable port. The generator reads its template and metadata from that commit.

Source packaging uses committed Git content, never the whole working directory.
Credentials, raw journals and build output are excluded and checked again in
both archives. No dependency DLLs or compiler-specific binary SDK is included.
Future binary releases must preserve LGPL notices and corresponding source.

See [publishing setup](PUBLISHING.md) for GitHub settings and Conan remote upload.
