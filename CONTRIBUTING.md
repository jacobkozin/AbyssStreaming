# Contributing

Thanks for helping improve Abyss Stream. The project is an early Windows prototype, so small, testable changes are especially valuable.

## Before opening a change

1. Do not commit stream keys, account identifiers, OBS logs, crash dumps, downloaded dependencies, or build output.
2. Keep the core layer independent of Qt and libobs when practical.
3. Run the core test configuration:

   ```powershell
   cmake -S . -B build/core -DABYSS_BUILD_UI=OFF -DABYSS_WITH_OBS=OFF -DABYSS_BUILD_TESTS=ON
   cmake --build build/core --config Release
   ctest --test-dir build/core -C Release --output-on-failure
   ```

4. For capture, encoding, or runtime-loading changes, also run `BUILD_AND_RUN.bat` on Windows and describe the hardware/runtime used.

## Pull requests

- Explain the user-facing problem and the chosen approach.
- Keep unrelated formatting or refactors out of the same change.
- Add or update tests for controller and scene-model behavior.
- Document manual validation when automated coverage is not possible.
- Scrub usernames, paths, server addresses, and credentials from attached logs.

Bug reports are useful, but public issues must never contain a real stream key or other account secret.
