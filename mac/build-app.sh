#!/bin/sh
# Builds build/PSPDeck.app and signs it with an Apple Development identity (stable signature, so the
# Accessibility permission survives rebuilds). SIGN_ID overrides; `./build-app.sh install` copies to ~/Applications.
set -eu
cd "$(dirname "$0")"
swift build -c release
APP=build/PSPDeck.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
cp .build/release/PSPDeck "$APP/Contents/MacOS/"
cp Info.plist "$APP/Contents/"
ID="${SIGN_ID:-$(security find-identity -v -p codesigning | awk -F'"' '/Apple Development/ {print $2; exit}')}"
codesign --force --sign "${ID:--}" "$APP"
if [ "${1:-}" = install ]; then
    mkdir -p "$HOME/Applications"
    rm -rf "$HOME/Applications/PSPDeck.app"
    cp -R "$APP" "$HOME/Applications/"
    echo "installed to ~/Applications/PSPDeck.app"
fi
