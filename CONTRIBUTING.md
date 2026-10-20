# Contributing

Open a focused issue or pull request describing the behavior you want to change.
Include a small reproducer and explain the user-account use case. Keep the API
short, model lifetimes explicit and new behavior consistent with `docs/API.md`.

Build with MSYS2 MINGW64 or a supported C++20 toolchain. Run the offline suite:

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

For transport or lifecycle changes, cover a real failure mode in the loopback
fixture. Check both static and shared builds for packaging changes. Update the
API docs/changelog when behavior changes. Do not add tests that contact Discord to
CI. Live examples must remain opt-in and use explicitly designated test accounts.

Never attach credentials, private profiles, message histories or raw live journals
to issues/PRs. Use fake IDs and redact captures. Do not add account-verification
bypass, browser impersonation or mass unsolicited messaging features.

Use four-space indentation and clear C++20 code. Match nearby code instead of
performing unrelated formatting changes. Contributions are submitted under
LGPL-3.0-or-later; retain applicable third-party notices when adding external code.
AI-assisted changes are welcome when their behavior, provenance and tests are reviewed.
