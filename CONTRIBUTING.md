# Contributing

Thanks for helping improve Abyss Stream. Small, tested changes are the easiest to review.

## Before opening a change

1. Never commit stream keys, account identifiers, logs, crash dumps, downloaded dependencies or build output.
2. Keep `src/core` free of Qt and libobs, and keep the UI talking to the core interfaces rather than libobs. That is what keeps the code testable and the engine replaceable.
3. Run the unit tests:

   ```powershell
   cmake -S . -B build/core -DABYSS_BUILD_UI=OFF -DABYSS_WITH_OBS=OFF -DABYSS_BUILD_TESTS=ON
   cmake --build build/core --config Release
   ctest --test-dir build/core -C Release --output-on-failure
   ```

4. For changes to capture, encoding, streaming or recording, also run `BUILD_AND_RUN.bat --no-launch` and `tools\run_e2e_test.ps1` on Windows, and say which GPU you tested on.

## Pull requests

- Explain the user-facing problem and the chosen approach.
- Keep unrelated formatting or refactors out of the same change.
- Add or update tests for controller, validation, settings and scene-model behavior. Use the `TEST_CASE`/`CHECK` macros in `tests/TestHarness.hpp`, not `assert()` (asserts vanish in Release builds).
- Describe manual testing where automated coverage is not possible.
- Scrub usernames, paths, server addresses and credentials from attached logs.

Bug reports are welcome, but public issues must never contain a real stream key or other account secret.
