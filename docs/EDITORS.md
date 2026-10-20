# Editor hover documentation

The public headers ship Doxygen comments for classes, methods, event/model fields,
parameters, return values and important failure/lifetime behavior. Hover over
`discord::Client`, `Message::reply`, `Channel::history`, `user.bio` or
`ClientOptions::request_timeout` in a configured C++ editor to read them.

The comments are installed with the headers through CMake, Conan, vcpkg and the
MSYS2 packages. No Doxygen executable, generated website or extra documentation
extension is required to read the hover text.

## VS Code: C/C++ and CMake Tools

1. Install Microsoft's **C/C++** (`ms-vscode.cpptools`) and **CMake Tools**
   (`ms-vscode.cmake-tools`) extensions. The repository recommends both.
2. Open the repository folder. Use **CMake: Select Configure Preset** and choose
   `mingw64` on the existing MSYS2 MINGW64 installation, or `default` in a
   compiler-configured Linux/MSYS2 terminal.
3. Run **CMake: Configure**. The checked-in workspace setting makes CMake Tools
   the IntelliSense configuration provider, so include directories, compiler
   definitions and the C++20 language standard come from the selected target.
4. Open an example, hover over a library symbol, or start typing a method call
   for parameter help. **Ctrl+K, Ctrl+I** also opens the hover in VS Code.

The `mingw64` preset expects `C:/msys64`. Adjust its compiler paths if MSYS2 is
installed elsewhere. In your own application, link `discord::self` through CMake
and configure that application's target; its IntelliSense then sees the library's
installed public headers. This repository's `.vscode` settings are not global.

If the header cannot be found or standard-library names have red squiggles,
configure the correct target/compiler first. Hover documentation depends on a
successful parse, not merely opening a folder containing the files.

## clangd, including VS Code and other LSP editors

Use clangd instead of a second simultaneous C++ language server. Configure a
preset first; both presets export `compile_commands.json`. Point the editor's
clangd process at the chosen build directory, for example:

```json
{
  "clangd.arguments": [
    "--compile-commands-dir=build/mingw64",
    "--query-driver=C:/msys64/mingw64/bin/g++.exe"
  ]
}
```

`--query-driver` allows clangd to ask that trusted compiler for system include
paths. On Linux, use `build/default` and your compiler path. If you use clangd in
VS Code, disable C/C++ IntelliSense for that workspace to avoid duplicate servers;
the CMake Tools extension can still configure/build the project.

The repository's `.clangd` sets C++20 explicitly. This matters when GCC's default
is already C++20 and CMake emits no `-std` flag, but clangd defaults to C++17.
It also requests Doxygen rendering on recent clangd versions. Older versions may
display the documentation as plain text instead of rich parameter sections.

## Maintainer verification

`tests/hover.cpp` is compiled but never executed. The language-server check
queries real type, method and field hovers with the configured compilation database:

```bash
python scripts/check_hover.py --build-dir build/default
```

In MINGW64, install `mingw-w64-x86_64-clang-tools-extra` and run:

```bash
python scripts/check_hover.py --build-dir build/mingw64 \
  --clangd /mingw64/bin/clangd.exe \
  --query-driver=C:/msys64/mingw64/bin/g++.exe
```

This tool uses fake source calls for analysis only; it does not connect to Discord.
The ordinary build does not require clangd.

References: [VS Code IntelliSense configuration](https://code.visualstudio.com/docs/cpp/configure-intellisense),
[clangd configuration](https://clangd.llvm.org/config.html).
