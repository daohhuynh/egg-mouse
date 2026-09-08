#!/bin/bash
# update-tap.sh -- regenerate the Homebrew formula for a released tag.
#
#   ./Tools/update-tap.sh 1.0.0 [OUTPUT.rb]
#
# Takes packaging/egg-mouse.rb as the template, points its `url` at the release
# tarball GitHub generates for tag v<version>, and fills in the `sha256` by
# actually downloading that tarball and hashing it. Writes to OUTPUT.rb, or to
# stdout if no path is given.
#
# The hash is COMPUTED, never typed. A formula whose sha256 was copied by hand
# is a formula that installs whatever the URL happens to serve, and Homebrew's
# integrity check is the only thing standing between a user and that.
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="${1:-}"
OUT="${2:-}"
[ -n "$VERSION" ] || { echo "usage: $0 <version> [output.rb]   e.g. $0 1.0.0" >&2; exit 2; }

REPO="daohhuynh/egg-mouse"
URL="https://github.com/$REPO/archive/refs/tags/v$VERSION.tar.gz"

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
echo "==> fetching $URL" >&2
curl -fsSL --retry 3 -o "$TMP/src.tar.gz" "$URL" \
  || { echo "could not fetch the tarball. Is tag v$VERSION pushed?" >&2; exit 1; }

# A 404 page would hash happily, so check it is really a gzip archive holding
# this project before trusting the bytes.
tar tzf "$TMP/src.tar.gz" >/dev/null 2>&1 \
  || { echo "downloaded file is not a tar.gz" >&2; exit 1; }
tar tzf "$TMP/src.tar.gz" | grep -q 'CMakeLists.txt$' \
  || { echo "tarball has no CMakeLists.txt; wrong archive" >&2; exit 1; }

SHA="$(shasum -a 256 "$TMP/src.tar.gz" | awk '{print $1}')"
echo "==> sha256 $SHA" >&2

python3 - "$VERSION" "$URL" "$SHA" > "$TMP/egg-mouse.rb" <<'PY'
import re, sys
version, url, sha = sys.argv[1], sys.argv[2], sys.argv[3]
src = open("packaging/egg-mouse.rb", encoding="utf-8").read()
src, n1 = re.subn(r'^  url ".*"$', '  url "%s"' % url, src, flags=re.M)
src, n2 = re.subn(r'^  sha256 ".*"$', '  sha256 "%s"' % sha, src, flags=re.M)
assert n1 == 1, "expected exactly one url line, patched %d" % n1
assert n2 == 1, "expected exactly one sha256 line, patched %d" % n2
assert "REPLACED_BY_UPDATE_TAP" not in src, "placeholder survived substitution"
assert version in src, "version %s not present in the generated formula" % version
sys.stdout.write(src)
PY

if [ -n "$OUT" ]; then
  mkdir -p "$(dirname "$OUT")"
  cp "$TMP/egg-mouse.rb" "$OUT"
  echo "==> wrote $OUT" >&2
else
  cat "$TMP/egg-mouse.rb"
fi
