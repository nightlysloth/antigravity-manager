# Changelog

All notable changes to this project are documented here.
Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [1.0.0]

### Added
- Status table showing installed version, latest version and state for App, IDE, CLI and SDK.
- Detection of installed components and their versions, including the Electron-based App (version read from `app.asar`).
- Automatic latest-release lookup from Google's download page (IDE and App links), with manual URL/file fallback.
- Installs missing components; "install/update everything that needs it" menu option.
- Non-interactive mode: `--check` (exit code 10 when action is needed), `--update TARGET`, `--yes`.
- `--keep-backup`, `--archive FILE`, `--page-file FILE` options.
- Automatic rollback when a new install fails verification.
- Running-application warning, trusted-host allow-list for downloads, shell-quoting of all paths.
- Offline smoke tests, Makefile and GitHub Actions workflows.

### Changed
- Backups, staging folders and downloads are cleaned up automatically.
- CLI updates use `agy update` first, then Google's installer script.
- An existing `.desktop` launcher is never overwritten.
- `--archive` always installs the supplied file, even if the installed version looks current.
