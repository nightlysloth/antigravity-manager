#!/usr/bin/env bash
# Offline smoke test: fake install, fixture download page, local archive.
set -euo pipefail

BIN=${1:-./antigravity-manager}
HERE=$(cd "$(dirname "$0")" && pwd)
PAGE="$HERE/fixtures/download-page.html"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

export HOME="$TMP/home"
IDE="$HOME/Applications/antigravity-official/Antigravity IDE"
mkdir -p "$IDE/resources/app"
echo '{"ideVersion": "2.5.0"}' > "$IDE/resources/app/product.json"
printf '#!/bin/sh\n' > "$IDE/antigravity-ide"
chmod +x "$IDE/antigravity-ide"

echo "[1/3] --check detects an outdated IDE (exit code 10)"
set +e
out=$("$BIN" --page-file "$PAGE" --check)
rc=$?
set -e
[ "$rc" -eq 10 ]
grep -q "UPDATE AVAILABLE" <<<"$out"

echo "[2/3] --update ide installs from a local archive and cleans up"
mkdir -p "$TMP/pkg/Antigravity IDE/resources/app"
echo '{"ideVersion": "2.6.0"}' > "$TMP/pkg/Antigravity IDE/resources/app/product.json"
printf '#!/bin/sh\n' > "$TMP/pkg/Antigravity IDE/antigravity-ide"
chmod +x "$TMP/pkg/Antigravity IDE/antigravity-ide"
tar -czf "$TMP/ide.tar.gz" -C "$TMP/pkg" "Antigravity IDE"
"$BIN" --page-file "$PAGE" --archive "$TMP/ide.tar.gz" --update ide --yes >/dev/null
grep -q '"ideVersion": "2.6.0"' "$IDE/resources/app/product.json"
[ ! -e "$IDE.agm-backup" ]
[ ! -e "$HOME/Applications/antigravity-official/.agm-stage" ]

echo "[3/3] a corrupt archive leaves the installation untouched"
echo '{"ideVersion": "2.5.0"}' > "$IDE/resources/app/product.json"   # outdated vs. fixture (2.6.0)
echo junk > "$TMP/bad.tar.gz"
if "$BIN" --page-file "$PAGE" --archive "$TMP/bad.tar.gz" --update ide --yes >/dev/null 2>&1; then exit 1; fi
grep -q '"ideVersion": "2.5.0"' "$IDE/resources/app/product.json"
[ -x "$IDE/antigravity-ide" ]

echo "smoke tests passed"
