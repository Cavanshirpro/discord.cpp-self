# GitHub and package publishing setup

This is Cavanshirpro's personal project. The repository is
`Cavanshirpro/discord.cpp-self`. The current GitHub release is **v0.1.1**, with
package version **0.1.1**. It is a normal Release; v0.1 remains a historical prerelease.

## Accounts you need

| Distribution method | Account needed |
| --- | --- |
| GitHub Release, CMake, source archives | Your existing GitHub account |
| vcpkg overlay | No additional publisher account |
| Building the Conan recipe locally | No registry account |
| Central vcpkg submission | Existing GitHub account; reviewed pull request |
| ConanCenter submission | Existing GitHub account and its contributor agreement |
| Upload to your own Conan remote | An account/service credential on a Conan server, such as Artifactory |

An additional hosting account is optional for v0.1. Users can install from source
or the overlay immediately, and can build the Conan recipe in their local cache.

## GitHub repository settings

1. Open **Settings > Actions > General**. Enable GitHub Actions and allow the
   actions used by this repository (`actions/*` and `msys2/setup-msys2`).
2. Under **Workflow permissions**, allow read and write permissions if your
   account/organization policy otherwise blocks release creation. The workflows
   themselves use read-only access by default and grant `contents: write` only
   to the release job. No PAT, Discord token, or package credential is needed
   for a GitHub Release.
3. Set the repository description and homepage to the project/documentation URL.
   Select the README as the overview and let GitHub detect the LGPL license.
4. After the first CI runs, you can require the **build** and **packages** checks
   under **Settings > Rules > Rulesets** for `main`. Select the actual matrix
   job names from GitHub's check picker. Do not require a check that never runs
   on the event/branch covered by your rule.

GitHub supplies `GITHUB_TOKEN` for each run. Do not create a secret with that name.
The release workflow never logs into Discord. Local `test_token`, environment
files and live journals stay outside Git and release archives.

## GitHub Release

Push `main`, inspect **Actions > build** and **Actions > packages**, then push
the annotated release tag as described in [Releasing](RELEASING.md). The
**publish** workflow runs for `v*` tags. It verifies Linux/Windows static/shared
builds, the Conan package and the vcpkg overlay before creating the release.

The release contains two source archives, a vcpkg overlay ZIP, `release.md` and
SHA256 checksums for all four files. English release notes also appear on the
Release page. Downloadable source is built from the
tag's commit, not the working directory. These are source packages, not a
universal Windows/Linux binary SDK.

To retry publication, open **Actions > publish > Run workflow**, select the
workflow branch and enter the existing tag `v0.1.1`. Existing releases are not
silently replaced. For a code correction after publication, increment the
version, write new release notes and create a new tag.

## Publish to a Conan remote

Conan packages can be hosted on a compatible remote such as JFrog Artifactory.
Create/select a Conan repository there and copy its Conan API URL. GitHub
Packages is not a native Conan remote.

In this GitHub repository:

1. Open **Settings > Environments > New environment**, name it `conan-publish`.
   You can add required reviewers if you want manual upload approval.
2. In that environment's **Variables**, add:

   | Variable | Value |
   | --- | --- |
   | `CONAN_REMOTE_URL` | Your remote's Conan API URL |

3. In its **Environment secrets**, add:

   | Secret | Value |
   | --- | --- |
   | `CONAN_LOGIN_USERNAME` | The remote's service account/user name |
   | `CONAN_PASSWORD` | Its token or password with upload permission |

4. Open **Actions > publish-conan > Run workflow**. Use tag `v0.1.1` and leave
   **upload** unchecked for a build-only trial. This builds and executes consumers
   for static and shared Release packages on the Linux runner.
5. Run again with **upload** checked to authenticate and upload
   `discord-cpp-self/0.1.1`. Only this library is uploaded; public dependencies
   continue to come from ConanCenter.

The workflow verifies that the matching GitHub release exists. It uses
Conan's remote-specific authentication environment variables; missing credentials
fail upload instead of prompting. This initial publishing job produces Linux
packages for its detected compiler profile, not Windows/macOS binaries. Extend
the runner/profile matrix when those package variants have been validated.

After upload, users add the remote:

```bash
conan remote add cavanshirpro <your-conan-api-url>
conan install --requires=discord-cpp-self/0.1.1 --build=missing -s compiler.cppstd=20
```

Private remotes additionally require user authentication. A recipe alone still
allows consumers to build missing binary configurations from source.

## Submit to ConanCenter

ConanCenter has a reviewed contribution process, not a GitHub secret that pushes
an arbitrary recipe directly into its index. Once v0.1.1 is public:

1. Fork [conan-center-index](https://github.com/conan-io/conan-center-index).
   Review its Contributor License Agreement; acceptance requires the contributor's signature.
2. Add the recipe in `recipes/discord-cpp-self/all/`, with version mapping in
   `config.yml` and the release source URL/SHA256 in `conandata.yml`.
3. Adapt the repository-local `exports_sources` recipe to a `source()` method
   using Conan's `get(..., strip_root=True)` and the checksum from `conandata.yml`.
   Keep the dependencies, validation and package consumer test.
4. Follow the current [ConanCenter contribution guide](https://github.com/conan-io/conan-center-index/blob/master/CONTRIBUTING.md),
   test the required configurations, and open a pull request.

The included recipe is usable now for local builds and a custom remote. Acceptance
and any name/policy changes remain the registry maintainers' decision.

## Submit to the central vcpkg registry

The release overlay already works without registry approval. To propose a central
port:

The curated registry currently expects a mature project: normally a release or
active public development history of at least six months, or equivalent maturity
accepted by its maintainers. A new v0.1.1 should use the working overlay first;
these steps prepare a later submission rather than promise immediate acceptance.


1. Fork [microsoft/vcpkg](https://github.com/microsoft/vcpkg).
2. Copy the generated release port directory to `ports/discord-cpp-self/`.
   Use the generated files, not `portfile.cmake.in`; the commit and SHA512 are real.
3. Run `./vcpkg format-manifest ports/discord-cpp-self/vcpkg.json`.
4. Test `./vcpkg install discord-cpp-self --triplet x64-linux` (and additional
   supported triplets), then `./vcpkg x-add-version discord-cpp-self`.
5. Commit the port and generated `versions/` changes, then open a pull request
   following the [vcpkg contribution guide](https://learn.microsoft.com/en-us/vcpkg/contributing/maintainer-guide).

Central registry submissions are not performed by this repository's release
workflow. Never advertise a central package badge before the port is accepted.

## Other distribution channels

CMake FetchContent, vendoring, source installs and pkg-config need no external
registry account. Linux distribution packages, Homebrew formulae, and MSYS2
packages each have separate packaging and review policies. Add those after
validating their platform/toolchain rather than uploading one incompatible
binary everywhere. PyPI and npm are not native C++ library registries.

## References

- [GitHub release automation](https://docs.github.com/en/repositories/releasing-projects-on-github/automatically-generated-release-notes)
- [GitHub Packages supported registries](https://docs.github.com/en/packages/learn-github-packages/introduction-to-github-packages)
- [Conan uploading](https://docs.conan.io/2/tutorial/conan_repositories/uploading_packages.html)
- [Conan authentication variables](https://docs.conan.io/2/reference/environment.html)
- [vcpkg_from_github](https://learn.microsoft.com/en-us/vcpkg/maintainers/functions/vcpkg_from_github)
