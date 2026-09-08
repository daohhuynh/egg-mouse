#!/bin/bash
# make-release.sh -- build the downloadable .dmg, and check it is actually
# downloadable rather than assuming it.
#
#   ./Tools/make-release.sh 1.0.0
#
# Produces dist/EGG-Mouse-<version>.dmg containing a universal, self-contained
# "EGG Mouse.app": both CLIs inside the bundle, no dependency outside /System
# and /usr/lib, and both architectures in every executable.
#
# WHAT THIS CANNOT DO, said here because it is the thing people assume. The
# result is AD-HOC SIGNED, not Developer ID signed and not notarized, because
# notarization requires a paid Apple Developer account. Gatekeeper will show a
# malware warning on first launch and the user has to allow it through System
# Settings > Privacy & Security. The Homebrew tap in packaging/ is the route
# that avoids that entirely; this is for people who do not have Homebrew.
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="${1:-}"
[ -n "$VERSION" ] || { echo "usage: $0 <version>   e.g. $0 1.0.0" >&2; exit 2; }

APP="dist/EGG Mouse.app"
DMG="dist/EGG-Mouse-$VERSION.dmg"
BUILD="build-release"

rm -rf dist "$BUILD"
mkdir -p dist

echo "==> universal CMake build (arm64 + x86_64)"
# EGG_VENDORED_HIDAPI is ON by default and must stay on here: Homebrew's
# libhidapi is arm64-only and lives at an absolute path, so linking it would
# make both the universal slice and the "runs without Homebrew" property
# impossible. See third_party/hidapi/README.md.
cmake -S . -B "$BUILD" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      -DEGG_VENDORED_HIDAPI=ON >/dev/null
cmake --build "$BUILD" --target egg-config egg-flash -j"$(sysctl -n hw.ncpu)" >/dev/null

echo "==> universal app bundle with the CLIs inside it"
./Tools/build-app.sh --universal --with-tools "$BUILD" --version "$VERSION" --out "$APP"

echo "==> checking the artefact before shipping it"
fail=0
check () { if [ "$2" = "$3" ]; then printf "    ok    %s\n" "$1"; else printf "    FAIL  %s (got %s, want %s)\n" "$1" "$2" "$3"; fail=1; fi; }

for exe in "EGG Mouse" egg-config egg-flash; do
  p="$APP/Contents/MacOS/$exe"
  check "$exe is universal" "$(lipo -archs "$p" | tr ' ' '\n' | sort | tr '\n' ' ')" "arm64 x86_64 "
  # The whole point of vendoring hidapi. Anything here outside /System or
  # /usr/lib is a machine-specific path that will not exist on a stranger's Mac.
  # Dependency lines are TAB-indented; a universal binary also prints an
  # untabbed "<path> (architecture arm64):" header per slice, and an earlier
  # version of this check read those headers as if they were dependencies and
  # failed a bundle that was fine.
  outside=$(otool -L "$p" | grep '^	' | awk '{print $1}' | sort -u \
            | grep -vE '^(/System/|/usr/lib/)' || true)
  check "$exe has no non-system deps" "${outside:-none}" "none"
  codesign --verify --strict "$p" 2>/dev/null \
    && printf "    ok    %s signature valid\n" "$exe" \
    || { printf "    FAIL  %s signature invalid\n" "$exe"; fail=1; }
done
codesign --verify --strict --deep "$APP" 2>/dev/null \
  && printf "    ok    bundle signature valid\n" \
  || { printf "    FAIL  bundle signature invalid\n"; fail=1; }
[ "$fail" -eq 0 ] || { echo "==> refusing to package a broken bundle"; exit 1; }

echo "==> first-launch note, in the disk image where it will be read"
cat > "dist/READ ME FIRST.txt" <<TXT
EGG Mouse $VERSION
Unofficial. Not affiliated with or endorsed by Endgame Gear GmbH.

INSTALL
  Drag "EGG Mouse" onto the Applications folder in this window.

FIRST LAUNCH: macOS WILL WARN YOU, AND HERE IS WHY
  This app is not signed with an Apple Developer certificate, because that
  costs 99 USD a year and this is a free project. macOS therefore cannot
  check who built it, and says so in strong terms.

  1. Double-click the app. macOS says it "cannot be opened". Click Done.
  2. Open System Settings > Privacy & Security.
  3. Scroll to Security. There is a line about "EGG Mouse" being blocked.
     Click "Open Anyway", and authenticate.
  4. Launch it again and click Open.

  You only do this once.

  If you would rather not, and you have Homebrew, this installs the same
  software with no warning at all, because software Homebrew compiles on your
  own machine is never quarantined:

      brew install daohhuynh/egg-mouse/egg-mouse

WHAT IT DOES
  Settings   reads and changes your mouse's configuration.
  Firmware   backs up the installed firmware, and writes a new one.

  egg-flash REWRITES FIRMWARE. It is provided AS IS, without warranty of any
  kind, and a failed flash may leave a mouse that does not work. Read the
  LICENSE and NOTICE files in the repository before using the Firmware screen.

  If your mouse ever stops responding: hold the LEFT and RIGHT buttons
  together, plug the cable in while still holding, keep holding a few more
  seconds, then release. It comes back as a re-flashable bootloader.

SOURCE
  https://github.com/daohhuynh/egg-mouse
TXT
cp LICENSE NOTICE dist/
# BSD-3-Clause requires the hidapi notice and disclaimer to accompany a BINARY
# redistribution, and this disk image is one. Not optional, so not `|| true`.
cp third_party/hidapi/LICENSE-bsd.txt "dist/LICENSE-hidapi-bsd.txt"
cp third_party/hidapi/AUTHORS.txt     "dist/AUTHORS-hidapi.txt"
ln -s /Applications dist/Applications

echo "==> hdiutil"
rm -f "$DMG"
hdiutil create -quiet -volname "EGG Mouse $VERSION" \
        -srcfolder dist -ov -format UDZO "$DMG.tmp.dmg"
mv "$DMG.tmp.dmg" "$DMG"

echo
echo "built: $DMG"
echo "  size:    $(du -h "$DMG" | cut -f1 | tr -d ' ')"
echo "  sha256:  $(shasum -a 256 "$DMG" | awk '{print $1}')"
