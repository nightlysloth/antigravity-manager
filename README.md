# Antigravity Manager

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A small, dependency-free C program that installs, updates and cleans up the
Google Antigravity toolchain on **Linux** and **Windows** from one place:

| Component | How it is detected | How it is installed / updated |
|-----------|--------------------|-------------------------------|
| **Antigravity App** | `resources/app/product.json` (`ideVersion`) | Official package from Google, replaced atomically |
| **Antigravity IDE** | `resources/app/product.json` (`ideVersion`) | Official package from Google, replaced atomically |
| **Antigravity CLI** (`agy`) | `agy --version` | `agy update`, falling back to Google's official install script |
| **Python SDK** (`google-antigravity`) | `pip show` in a private virtual environment | `pip install --upgrade` |

> **Unofficial.** This project is not affiliated with or endorsed by Google.
> It only talks to Google-owned hosts and never modifies your projects,
> settings, extensions or login data.

## Features

- **Status at a glance:** installed version, latest version and state for every component.
- **Installs what is missing,** updates what is outdated, skips what is current.
- **Automatic latest-release lookup** from Google's download page, with a manual fallback (paste a URL or a file path).
- **Safe replacement:** new version is extracted to a staging folder first, the old one is moved aside, the new one is verified, and only then is the backup deleted. If verification fails, the old version is restored.
- **Self-cleaning:** downloaded archives, staging folders and backups are removed automatically (`--keep-backup` to opt out).
- **Scriptable:** `--check` and `--update` with meaningful exit codes, no prompts with `--yes`.
- **Trusted hosts only:** downloads are restricted to `https://` URLs on Google hosts.
- **Single C11 source file,** no third-party libraries. Uses the system `curl` and (on Linux) `tar`.

## Install

### Build from source

```sh
git clone https://github.com/nightlysloth/antigravity-manager.git
cd antigravity-manager
make                 # builds ./antigravity-manager
make test            # offline smoke tests (Linux)
sudo make install    # optional: installs to /usr/local/bin
```

Without `make`:

```sh
cc -std=c11 -O2 -Wall -Wextra src/antigravity-manager.c -o antigravity-manager
```

**Windows** (MinGW-w64 / MSYS2):

```sh
gcc -std=c11 -O2 -Wall -Wextra src/antigravity-manager.c -o antigravity-manager.exe
```

### Requirements

- `curl` (preinstalled on Windows 10+ and nearly all Linux distributions)
- `tar` (Linux, for App/IDE archives)
- `python3` / `py` (only for the Python SDK)

## Usage

```text
antigravity-manager                  interactive menu with status table
antigravity-manager --check          print status only
antigravity-manager --update ide     update or install: app | ide | cli | sdk | all
```

| Option | Meaning |
|--------|---------|
| `--check` | Print the status table. Exit code **10** if anything is missing or outdated, otherwise **0**. |
| `--update TARGET` | Non-interactive update/install. `all` only touches components that need it. Exit code 0 on success, 1 on failure. |
| `--yes`, `-y` | Never prompt; uses safe defaults (never deletes files you supplied, skips if the app is running). |
| `--keep-backup` | Keep the previous version after a successful update. |
| `--archive FILE` | Install the App/IDE from a local archive instead of downloading. |
| `--page-file FILE` | Read the download page from a local file (offline use and testing). |
| `--help` | Show usage. |

Example status output:

```text
  Component                 Installed   Latest      Status
  ------------------------  ----------  ----------  ------------------------------
  Antigravity App           -           2.6.1       NOT INSTALLED
  Antigravity IDE           2.5.0       2.6.0       UPDATE AVAILABLE
  Antigravity CLI (agy)     -           ?           NOT INSTALLED
  Antigravity Python SDK    -           ?           NOT INSTALLED
```

### Automating checks

```sh
# e.g. from cron or a systemd timer: update everything quietly
antigravity-manager --update all --yes
```

## Where things are installed

**Linux** (first existing location wins when detecting; the first row is used for new installs):

| Component | Locations checked |
|-----------|-------------------|
| IDE | `$ANTIGRAVITY_IDE_DIR`, `~/Applications/antigravity-official/Antigravity IDE`, `~/.local/opt/antigravity-ide`, `/opt/antigravity-ide`, `/opt/Antigravity IDE` |
| App | `$ANTIGRAVITY_APP_DIR`, `~/.local/opt/antigravity-app`, `~/Applications/antigravity-official/Antigravity`, `/opt/Antigravity`, `/opt/antigravity` |
| CLI | wherever `agy` is on `PATH`, or `~/.local/bin/agy` |
| SDK | `~/.local/share/antigravity-sdk-venv` |

Set `ANTIGRAVITY_IDE_DIR` / `ANTIGRAVITY_APP_DIR` to use a custom location. If the
folder is not writable (for example under `/opt`), run with `sudo` or choose a folder you own.

A `.desktop` launcher is created in `~/.local/share/applications` only if one does not already exist.

**Windows:** App and IDE are installed by Google's own installer (run interactively). Detection looks under `%LOCALAPPDATA%\Programs`.

## How an update works (Linux App/IDE)

1. Resolve the latest package URL from Google's download page and download it.
2. Extract to a hidden staging folder next to the install (same filesystem, so the swap is a rename).
3. Locate the application's executable inside the archive; abort untouched if it is missing.
4. Warn if the app is running.
5. Move the current install to `<install>.agm-backup`, move the new one into place.
6. Verify the executable; **roll back** on failure.
7. Remove the backup, the staging folder and the download.

## Limitations

- Release discovery reads Google's download page. If Google changes how that page exposes links, automatic lookup falls back to asking for a URL or file; update `consider()`/`scan_text()` if needed.
- Only x86-64 packages are matched automatically.
- Windows support has been written but has had limited real-world testing. The App's Linux layout is a best guess.
- Third-party IDE extensions are intentionally not managed.
- Package checksums are not published in a form this tool can verify; integrity relies on HTTPS to Google hosts.

## Development

```sh
make test    # builds and runs tests/smoke.sh against a fake install
```

The smoke test needs no network: it fakes an installed IDE, feeds the parser a fixture page
(`tests/fixtures/download-page.html`), installs from a local archive and checks cleanup and
failure handling. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE)
