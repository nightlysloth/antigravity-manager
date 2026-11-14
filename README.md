# Antigravity Manager

A small C program that installs and updates Google Antigravity (App, IDE, CLI and Python SDK) on Linux and Windows.

> Unofficial. Not affiliated with Google. It only downloads from Google-owned hosts and never touches your projects, settings or extensions.

## What it does

- Shows the installed and latest version of each component
- Installs anything that is missing and updates anything that is outdated
- Replaces App/IDE safely (backup, verify, roll back on failure) and cleans up downloads and backups afterwards
- Works interactively or from scripts

| Component | Detected via | Installed / updated via |
|-----------|--------------|-------------------------|
| App, IDE | `resources/app/product.json` | Official package from Google |
| CLI (`agy`) | `agy --version` | `agy update`, or Google's install script |
| Python SDK | `pip show` in a private venv | `pip install --upgrade` |

## Build

Requires `curl` (and `tar` on Linux).

```sh
make                 # Linux
make test            # offline smoke tests
```

Or directly:

```sh
cc  -std=c11 -O2 -Wall -Wextra src/antigravity-manager.c -o antigravity-manager        # Linux
gcc -std=c11 -O2 -Wall -Wextra src/antigravity-manager.c -o antigravity-manager.exe    # Windows (MinGW-w64)
```

Prebuilt binaries are on the [Releases](../../releases) page.

## Usage

```sh
antigravity-manager                 # interactive menu
antigravity-manager --check         # status only
antigravity-manager --update ide    # app | ide | cli | sdk | all
```

| Option | Meaning |
|--------|---------|
| `--check` | Print status. Exit code 10 if something is missing or outdated, else 0 |
| `--update TARGET` | Install/update without the menu (`all` = only what needs it) |
| `--yes` | Never prompt |
| `--keep-backup` | Keep the previous version after updating |
| `--archive FILE` | Install App/IDE from a local archive instead of downloading |
| `--page-file FILE` | Read the download page from a file (offline/testing) |

The IDE is looked for in `~/Applications/antigravity-official/Antigravity IDE`, `~/.local/opt/antigravity-ide` and `/opt`. Set `ANTIGRAVITY_IDE_DIR` or `ANTIGRAVITY_APP_DIR` to use another folder. On Windows, the App and IDE are installed by Google's own installer.

## Limitations

- Latest-version lookup reads Google's download page. If it can't find the links, the tool asks you for a URL or file.
- Only x86-64 packages are matched.
- The Windows build has had limited real-world testing, and the App's Linux layout is a best guess.
- Third-party IDE extensions are not managed.

## License

[MIT](LICENSE)
