#!/usr/bin/env bash
# Offline smoke test: fake installs, fixture download page, local archives.
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

echo "[1/5] --check detects an outdated IDE (exit code 10)"
set +e
out=$("$BIN" --page-file "$PAGE" --check)
rc=$?
set -e
[ "$rc" -eq 10 ]
grep -q "UPDATE AVAILABLE" <<<"$out"

echo "[2/5] --update ide installs from a local archive and cleans up"
mkdir -p "$TMP/pkg/Antigravity IDE/resources/app"
echo '{"ideVersion": "2.6.0"}' > "$TMP/pkg/Antigravity IDE/resources/app/product.json"
printf '#!/bin/sh\n' > "$TMP/pkg/Antigravity IDE/antigravity-ide"
chmod +x "$TMP/pkg/Antigravity IDE/antigravity-ide"
tar -czf "$TMP/ide.tar.gz" -C "$TMP/pkg" "Antigravity IDE"
"$BIN" --page-file "$PAGE" --archive "$TMP/ide.tar.gz" --update ide --yes >/dev/null
grep -q '"ideVersion": "2.6.0"' "$IDE/resources/app/product.json"
[ ! -e "$IDE.agm-backup" ]
[ ! -e "$HOME/Applications/antigravity-official/.agm-stage" ]

echo "[3/5] a corrupt archive leaves the installation untouched"
echo '{"ideVersion": "2.5.0"}' > "$IDE/resources/app/product.json"   # outdated vs. fixture (2.6.0)
echo junk > "$TMP/bad.tar.gz"
if "$BIN" --page-file "$PAGE" --archive "$TMP/bad.tar.gz" --update ide --yes >/dev/null 2>&1; then exit 1; fi
grep -q '"ideVersion": "2.5.0"' "$IDE/resources/app/product.json"
[ -x "$IDE/antigravity-ide" ]

# --- Antigravity App: Electron layout, version stored inside resources/app.asar -------------

mkasar() {   # mkasar DIR VERSION  -> DIR/resources/app.asar containing package.json
  python3 - "$1" "$2" <<'PY'
import json, os, struct, sys
d, ver = sys.argv[1], sys.argv[2]
pkg = json.dumps({"name": "antigravity", "version": ver}, separators=(",", ":")).encode()
hdr = json.dumps({"files": {
    "node_modules": {"files": {"x": {"files": {"package.json": {"size": 2, "offset": "0"}}}}},
    "package.json": {"size": len(pkg), "offset": "2"}}}, separators=(",", ":")).encode()
L = len(hdr); pad = (4 - L % 4) % 4; payload = 4 + L + pad
os.makedirs(d + "/resources", exist_ok=True)
with open(d + "/resources/app.asar", "wb") as f:
    f.write(struct.pack("<IIII", 4, payload + 4, payload, L) + hdr + b"\0" * pad + b"{}" + pkg)
PY
}

APP="$HOME/Applications/antigravity-app"
mkasar "$APP" 2.25.0
printf '#!/bin/sh\n' > "$APP/antigravity"
chmod +x "$APP/antigravity"

echo "[4/5] installed App is detected (version read from app.asar) and compared with the latest"
set +e
out=$("$BIN" --page-file "$PAGE" --check)
set -e
grep -Eq 'Antigravity App +2\.25\.0 +2\.32\.0 +UPDATE AVAILABLE' <<<"$out"

echo "[5/5] --update app installs a new App from a local archive"
mkdir -p "$TMP/apppkg/Antigravity"
mkasar "$TMP/apppkg/Antigravity" 2.32.0
printf '#!/bin/sh\n' > "$TMP/apppkg/Antigravity/antigravity"
chmod +x "$TMP/apppkg/Antigravity/antigravity"
tar -czf "$TMP/app.tar.gz" -C "$TMP/apppkg" Antigravity
"$BIN" --page-file "$PAGE" --archive "$TMP/app.tar.gz" --update app --yes >/dev/null
set +e
out=$("$BIN" --page-file "$PAGE" --check)
set -e
grep -Eq 'Antigravity App +2\.32\.0 +2\.32\.0 +up to date' <<<"$out"
[ ! -e "$APP.agm-backup" ]

echo "smoke tests passed"
