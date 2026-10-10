#!/usr/bin/env bash
set -euo pipefail
mode="${1:-run}"
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
app_name="OpenDisplayNXInput"
bundle="$project_dir/dist/OpenDisplay NX Input.app"
if [ "$mode" != "--build-only" ]; then pkill -x "$app_name" >/dev/null 2>&1 || true; fi
swift build --package-path "$project_dir/companion" -c release
binary_dir="$(swift build --package-path "$project_dir/companion" -c release --show-bin-path)"
mkdir -p "$bundle/Contents/MacOS" "$bundle/Contents/Resources"
cp "$binary_dir/$app_name" "$bundle/Contents/MacOS/$app_name"
cp "$project_dir/assets/opendisplay-nx-neon-preview.png" "$bundle/Contents/Resources/Icon.png"
cat > "$bundle/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleExecutable</key><string>OpenDisplayNXInput</string>
<key>CFBundleIdentifier</key><string>io.github.IlieBanda.OpenDisplayNXInput</string>
<key>CFBundleName</key><string>OpenDisplay NX Input</string>
<key>CFBundleShortVersionString</key><string>0.2.0</string>
<key>CFBundleVersion</key><string>1</string>
<key>LSMinimumSystemVersion</key><string>13.0</string>
<key>NSPrincipalClass</key><string>NSApplication</string>
<key>NSLocalNetworkUsageDescription</key><string>Connect to your paired Nintendo Switch for controller and keyboard input.</string>
</dict></plist>
PLIST
codesign --force --sign - "$bundle"
case "$mode" in
  --build-only) ;;
  run) open -n "$bundle" ;;
  --verify) open -n "$bundle"; sleep 1; pgrep -x "$app_name" >/dev/null ;;
  --debug) lldb -- "$bundle/Contents/MacOS/$app_name" ;;
  --logs|--telemetry) open -n "$bundle"; /usr/bin/log stream --info --style compact --predicate 'process == "OpenDisplayNXInput"' ;;
  *) echo 'Usage: build_and_run.sh [--build-only|--verify|--debug|--logs|--telemetry]' >&2; exit 2 ;;
esac
