# MSYS2 package

The generated recipe builds from a specific source commit and verifies its
SHA512. It creates a shared library with headers (including hover comments),
CMake/pkg-config metadata and license notices. Dependencies come from MSYS2.

## Install a release package

Download the matching `.pkg.tar.zst` file and `SHA256SUMS` from GitHub Releases,
verify its checksum, and use the shell matching your compiler:

```bash
# MSYS2 MINGW64, matching the existing project toolchain:
pacman -U ./mingw-w64-x86_64-discord-cpp-self-0.1.1-1-any.pkg.tar.zst

# MSYS2 UCRT64, for an application built with the UCRT64 compiler:
pacman -U ./mingw-w64-ucrt-x86_64-discord-cpp-self-0.1.1-1-any.pkg.tar.zst
```

Choose one matching package, not both for a single application. MSYS2 resolves
the package's declared dependencies. Keep the MSYS2 installation up to date;
do not mix libraries from different runtime prefixes.

Use `find_package(discordSelf CONFIG REQUIRED)` and link `discord::self`. The
matching compiler normally finds its own `/mingw64` or `/ucrt64` install prefix.

These are project release packages, not packages already accepted into the
official MSYS2 repositories. `pacman -S ...-discord-cpp-self` is only available
after an official submission is accepted, or after configuring your own package
repository. Installing a downloaded local package uses `pacman -U`.

## Build from the generated recipe

Download/extract `discord.cpp-self-v0.1.1-msys2-recipe.zip`. Enter its
`msys2/mingw-w64-discord-cpp-self` directory in the matching MSYS2 shell:

```bash
pacman -S --needed base-devel
# Select exactly the environment you intend to build for:
MINGW_ARCH=mingw64 makepkg-mingw --syncdeps --cleanbuild --noconfirm
# Or, in UCRT64:
MINGW_ARCH=ucrt64 makepkg-mingw --syncdeps --cleanbuild --noconfirm
pacman -U ./*.pkg.tar.zst
```

`--syncdeps` installs the declared build/check/runtime dependencies. Offline unit
and loopback tests run in `check()`; no token is needed. The recipe is generated
from `PKGBUILD.in`, whose placeholders must be replaced before use.

From a public committed checkout, maintainers generate it with:

```bash
python scripts/make_msys2_recipe.py --ref v0.1.1
```

## Official MSYS2 submission

Your existing GitHub account is enough:

1. Fork [msys2/MINGW-packages](https://github.com/msys2/MINGW-packages).
2. Copy the generated `PKGBUILD` into
   `mingw-w64-discord-cpp-self/PKGBUILD` in that fork.
3. Build/test the declared environments with `makepkg-mingw`, then install the
   package with `pacman -U` and compile a CMake consumer.
4. Open a PR following the current packaging guidelines. The maintainers may
   request additional supported environments or recipe changes.
5. After acceptance and the official build, users can install the accepted
   package with `pacman -S` using their environment's package prefix.

The release recipe includes MINGW64 for the existing project toolchain and
UCRT64 for current UCRT applications. Official repository acceptance and its
current target-environment requirements are decided by MSYS2 maintainers.
No registry token or additional hosting account is needed for this PR route.

See the [official new-package guide](https://www.msys2.org/dev/new-package/)
and [package guidelines](https://www.msys2.org/dev/package-guidelines/).
