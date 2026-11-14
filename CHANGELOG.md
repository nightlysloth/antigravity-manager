# Changelog

All notable changes to this project are documented here.
Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [1.1.0]

### Added
- Antigravity App support: detects an installed App (including the Electron layout, where the version is stored inside `resources/app.asar`) and looks up its latest release from Google's download page.
- Downloads are allowed from Google's public Antigravity storage bucket in addition to the existing Google hosts.
- Offline tests for App detection and App updates.

### Changed
- `--archive` always installs the supplied file, even when the installed version looks current.
- App archives are only accepted if they contain the `antigravity` executable together with a `resources` folder.

## [1.0.0]

### Added
- Status table showing installed version, latest version and state for App, IDE, CLI and SDK.
- Automatic detection of installed components and their versions.
- Automatic latest-release lookup from Google's download page, with manual URL/file fallback.
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
