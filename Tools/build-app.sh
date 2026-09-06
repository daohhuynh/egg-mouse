#!/bin/bash
# build-app.sh -- compile the SwiftUI front end into build/EGG Mouse.app
#
# swiftc rather than an Xcode project, so the whole build is one file anyone can
# read and `cmake --build build` remains the only other step. The bundle layout
# is the minimum macOS needs to treat it as a real app: an Info.plist naming the
# executable, and LSMinimumSystemVersion low enough to survive an OS upgrade.
set -euo pipefail
cd "$(dirname "$0")/.."

APP="build/EGG Mouse.app"
BIN="$APP/Contents/MacOS/EGG Mouse"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

# Deployment target is deliberately behind the SDK (15.5 here). macOS 14 is the
# floor because SwiftUI's two-parameter onChange arrived there; going lower
# means using the deprecated one-parameter form, and a build full of deprecation
# warnings is a build whose real warnings get skimmed. Building against a newer
# SDK costs nothing here and keeps the bundle running if it is ever copied to an
# older Mac.
swiftc \
  -target arm64-apple-macosx14.0 \
  -O -swift-version 5 \
  -framework SwiftUI -framework AppKit \
  -o "$BIN" \
  Sources/EGGApp/*.swift

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
  <key>CFBundleShortVersionString</key> <string>1.0</string>
  <key>CFBundleVersion</key>         <string>1</string>
  <key>LSMinimumSystemVersion</key>  <string>14.0</string>
  <key>NSHighResolutionCapable</key> <true/>
  <key>NSHumanReadableCopyright</key>
  <string>Drives egg-config and egg-flash. It does not talk to the device itself.</string>
</dict>
</plist>
PLIST

echo "built: $APP"
echo
echo "It looks for egg-config and egg-flash next to itself, then in ./build."
echo "Run  cmake --build build  first, then:  open '$APP'"
