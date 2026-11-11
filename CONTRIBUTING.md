# Contributing

Thanks for helping improve Antigravity Manager.

## Ground rules

- Keep it a **single portable C11 file** with no third-party dependencies.
- Code must build warning-free with `-std=c11 -Wall -Wextra` on Linux (gcc/clang) and Windows (MinGW-w64).
- Anything passed to a shell must go through `Q()` (quoting) and downloads must pass `url_allowed()`.
- Never delete or modify user data; only the managed install folders, staging folders and files the tool downloaded itself.

## Workflow

```sh
make          # build
make test     # offline smoke tests (no network required)
```

1. Fork and create a topic branch.
2. Add or update a case in `tests/smoke.sh` for behaviour you change.
3. Update `CHANGELOG.md` under an "Unreleased" heading.
4. Open a pull request describing what you tested and on which OS.

## Reporting bugs

Include your OS, the output of `antigravity-manager --check`, and the failing command.
If release lookup fails, attach the (redacted) structure of Google's download page links.
