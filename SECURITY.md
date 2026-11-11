# Security Policy

## Reporting a vulnerability

Please report security issues privately through GitHub's **Security → Report a vulnerability**
feature on this repository rather than opening a public issue.

## Design notes

- Downloads are restricted to `https://` URLs on Google hosts (`url_allowed()`).
- All file paths and URLs interpolated into shell commands are quoted; values containing quote
  characters or newlines are rejected.
- The tool runs as the invoking user and only writes to install folders, its own staging folder
  and its cache (`~/.cache/antigravity-manager` or `%TEMP%\antigravity-manager`).
- Google's official CLI installer (`curl ... | bash` / `irm ... | iex`) is executed as published by
  Google; review it first if that matters in your environment.
