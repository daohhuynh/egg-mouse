#!/bin/bash
# build-app.sh -- compile the SwiftUI front end into build/EGG Mouse.app
#
# swiftc rather than an Xcode project, so the whole build is one file anyone can
# read and `cmake --build build` remains the only other step. The bundle layout
# is the minimum macOS needs to treat it as a real app: an Info.plist naming the
# executable, and LSMinimumSystemVersion low enough to survive an OS upgrade.
#
#   --universal          also build an x86_64 slice and lipo the two together
#   --with-tools DIR     copy egg-config and egg-flash from DIR into the bundle
#   --version X.Y.Z      CFBundleShortVersionString (default 1.0)
#   --out PATH           where to write the .app (default build/EGG Mouse.app)
#
# Plain `./Tools/build-app.sh` is unchanged from what it always did: a fast
# native-arch bundle that finds the CLIs in ./build. The flags exist for
# Tools/make-release.sh, which is the only caller that needs them.
set -euo pipefail
cd "$(dirname "$0")/.."

UNIVERSAL=0
TOOLS_DIR=""
VERSION="1.0"
APP="build/EGG Mouse.app"

while [ $# -gt 0 ]; do
  case "$1" in
    --universal)  UNIVERSAL=1; shift ;;
    --with-tools) TOOLS_DIR="$2"; shift 2 ;;
    --version)    VERSION="$2";  shift 2 ;;
    --out)        APP="$2";      shift 2 ;;
    *) echo "build-app.sh: unknown argument: $1" >&2; exit 2 ;;
  esac
done

# Said here rather than left to swiftc's own error, because this script is run
# by a Homebrew formula on other people's machines. The Command Line Tools are
# enough; full Xcode is not required.
command -v swiftc >/dev/null 2>&1 || {
  echo "build-app.sh: swiftc not found. The SwiftUI front end needs Apple's" >&2
  echo "  Swift compiler, which comes with the Command Line Tools:" >&2
  echo "      xcode-select --install" >&2
  echo "  The two command line tools build without it (cmake --build build)." >&2
  exit 1
}

BIN="$APP/Contents/MacOS/EGG Mouse"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

# Deployment target is deliberately behind whatever SDK is installed. macOS 14
# is the floor because SwiftUI's two-parameter onChange arrived there; going
# lower means using the deprecated one-parameter form, and a build full of
# deprecation warnings is a build whose real warnings get skimmed. Building
# against a newer SDK costs nothing here and keeps the bundle running if it is
# ever copied to an older Mac.
compile_slice () {   # $1 = target triple, $2 = output path
  swiftc \
    -target "$1" \
    -O -swift-version 5 \
    -framework SwiftUI -framework AppKit \
    -o "$2" \
    Sources/EGGApp/*.swift
}

if [ "$UNIVERSAL" -eq 1 ]; then
  # Two passes and lipo: swiftc emits one architecture per invocation.
  TMP="$(mktemp -d)"
  trap 'rm -rf "$TMP"' EXIT
  compile_slice arm64-apple-macosx14.0  "$TMP/app-arm64"
  compile_slice x86_64-apple-macosx14.0 "$TMP/app-x86_64"
  lipo -create "$TMP/app-arm64" "$TMP/app-x86_64" -output "$BIN"
  # lipo INVALIDATES the linker's ad-hoc signature, and an arm64 binary with a
  # broken signature is killed on launch rather than merely warned about. So
  # re-sign here, not as an afterthought in the release script.
  codesign --force --sign - --timestamp=none "$BIN"
else
  compile_slice "$(uname -m)-apple-macosx14.0" "$BIN"
fi

# The two CLIs, inside the bundle, so a downloaded .app is one self-contained
# thing. ToolRunner.locate() looks in Contents/MacOS first. Without this the
# app is only usable from a repo checkout, which is fine for development and
# useless for anyone who downloaded it.
if [ -n "$TOOLS_DIR" ]; then
  for t in egg-config egg-flash; do
    [ -x "$TOOLS_DIR/$t" ] || { echo "build-app.sh: $TOOLS_DIR/$t not found or not executable" >&2; exit 1; }
    cp "$TOOLS_DIR/$t" "$APP/Contents/MacOS/$t"
    codesign --force --sign - --timestamp=none "$APP/Contents/MacOS/$t"
  done
fi

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key>            <string>EGG Mouse</string>
  <key>CFBundleDisplayName</key>     <string>EGG Mouse</string>
  <key>CFBundleIdentifier</key>      <string>local.eggmouse.app</string>
  <key>CFBundleExecutable</key>      <string>EGG Mouse</string>
  <key>CFBundlePackageType</key>     <string>APPL</string>
  <key>CFBundleShortVersionString</key> <string>$VERSION</string>
  <key>CFBundleVersion</key>         <string>$VERSION</string>
  <key>LSMinimumSystemVersion</key>  <string>14.0</string>
  <key>NSHighResolutionCapable</key> <true/>
  <key>NSHumanReadableCopyright</key>
  <string>Drives egg-config and egg-flash. It does not talk to the device itself.</string>
</dict>
</plist>
PLIST

# Sign the bundle LAST. Adding files to Contents/ after signing invalidates the
# signature, so the order here is load-bearing rather than stylistic.
codesign --force --sign - --timestamp=none "$APP"

echo "built: $APP"
echo "  architectures: $(lipo -archs "$BIN")"
if [ -n "$TOOLS_DIR" ]; then
  echo "  bundled CLIs:  $(ls "$APP/Contents/MacOS" | grep -c '^egg-') of 2"
else
  echo
  echo "It looks for egg-config and egg-flash next to itself, then in ./build."
  echo "Run  cmake --build build  first, then:  open '$APP'"
fi
