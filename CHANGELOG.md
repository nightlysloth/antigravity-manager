# Changelog

All notable changes to this project are documented here.
Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow [Semantic Versioning](https://semver.org/).

## [2.0.0] - 2026-11-11

### Added
- Status table showing installed version, latest version and state for App, IDE, CLI and SDK.
- Automatic detection of installed components and their versions.
- Automatic latest-release lookup from Google's download page, with manual URL/file fallback.
- Installs missing components; "install/update everything that needs it" menu option.
- Non-interactive mode: `--check` (exit code 10 when action is needed), `--update TARGET`, `--yes`.
- `--keep-backup`, `--archive FILE`, `--page-file FILE` options.
- Automatic rollback when a new install fails verification.
- Running-application warning, trusted-host allow-list for downloads, shell-quoting of all paths.
- Offline smoke tests and a Makefile.

### Changed
- Staging happens next to the install directory so the swap is a same-filesystem rename.
- Backups, staging folders and downloads are cleaned up automatically.
- CLI updates use `agy update` first, then Google's installer script.
- An existing `.desktop` launcher is no longer overwritten.

## [1.0.0]

### Added
- Initial menu-driven updater for App, IDE, CLI and Python SDK.
