# Building on Windows

The full application is a native C++20/Qt 6 frontend linked to OBS/libobs 32.2.2. A core-only build is available without Qt or OBS.

## One-click development build

Requirements:

- Windows 10/11 x64
- Visual Studio 2022 or newer with **Desktop development with C++**
- Internet access for first-time dependency downloads

Run `BUILD_AND_RUN.bat` from the repository root. The helper:

1. Finds an x64 MSVC developer environment and CMake 3.28+.
2. Downloads the pinned OBS 32.2.2 runtime and source tag archive.
3. Downloads the matching OBS Qt 6 dependency package.
4. Verifies published SHA-256 values for the runtime and Qt archive, and validates the source archive structure.
5. Generates an import library from the matching `obs.dll` exports.
6. Configures and builds a Release executable with NMake.
7. Runs the core CTest target.
8. Stages the required runtime DLLs and launches Abyss Stream.

Dependencies are cached under `%LOCALAPPDATA%\AbyssStream\deps`. Build output, logs, and the recorded local paths live under `build/`; both locations are outside version control.

If Visual Studio C++ tools are missing, `INSTALL_BUILD_TOOLS_ONCE.bat` invokes Microsoft's official Build Tools package through `winget`. This is intentionally separate because the compiler installation is large and changes the machine.

After a successful build, `RUN_LAST_BUILD.bat` starts the recorded executable without compiling again.

## Manual core build

```powershell
cmake -S . -B build/core `
  -DABYSS_BUILD_UI=OFF `
  -DABYSS_WITH_OBS=OFF `
  -DABYSS_BUILD_TESTS=ON
cmake --build build/core --config Release
ctest --test-dir build/core -C Release --output-on-failure
```

## Manual full build

If your environment already exposes Qt 6 and an `OBS::libobs` CMake target:

```powershell
cmake -S . -B build/full `
  -DABYSS_BUILD_UI=ON `
  -DABYSS_WITH_OBS=ON `
  -DABYSS_BUILD_TESTS=ON
cmake --build build/full --config Release
ctest --test-dir build/full -C Release --output-on-failure
```

Alternatively, set `ABYSS_OBS_INCLUDE_DIR`, `ABYSS_OBS_LIBRARY`, and `CMAKE_PREFIX_PATH` as demonstrated by `tools/setup_build_and_run.ps1`.

At runtime, `ABYSS_OBS_RUNTIME` may point to the extracted OBS root containing `bin/64bit`, `obs-plugins/64bit`, and `data`. The included launcher sets this automatically.
