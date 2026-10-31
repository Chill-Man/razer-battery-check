#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
version=$(tr -d '\r\n' < VERSION)
machine=$(uname -m)
case "$machine" in x86_64) debarch=amd64;libc=2.35;libstd=11;; aarch64) debarch=arm64;libc=2.39;libstd=13;; *) echo "Unsupported architecture: $machine";exit 1;; esac
mkdir -p release build/linux-package/root build/linux-package/archive
root="$PWD/build/linux-package/root"
cmake --install build/linux --prefix "$root/usr"
install -Dm644 packaging/linux/70-basilisk-pulse.rules "$root/usr/lib/udev/rules.d/70-basilisk-pulse.rules"
install -Dm644 docs/LINUX.md "$root/usr/share/doc/basilisk-pulse/LINUX.md"
mkdir -p "$root/DEBIAN"
cat > "$root/DEBIAN/control" <<EOF
Package: basilisk-pulse
Version: $version
Section: utils
Priority: optional
Architecture: $debarch
Maintainer: Chill-Man <noreply@users.noreply.github.com>
Depends: libqt5widgets5 (>= 5.12), libqt5network5 (>= 5.12), libqt5gui5 (>= 5.12), libc6 (>= $libc), libstdc++6 (>= $libstd)
Recommends: qtwayland5
Homepage: https://github.com/Chill-Man/razer-battery-check
Description: Razer Basilisk battery tray monitor
 Native Linux preview with battery percentage, runtime estimation,
 local history and a compact desktop panel.
EOF
cat > "$root/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if command -v udevadm >/dev/null 2>&1; then udevadm control --reload-rules >/dev/null 2>&1 || true; fi
echo 'Basilisk Pulse: reconnect the receiver to apply the device access rule.'
EOF
chmod 755 "$root/DEBIAN/postinst"
dpkg-deb --root-owner-group --build "$root" "release/BasiliskPulse-$version-linux-$machine.deb"
archive=build/linux-package/archive
install -m755 build/linux/basilisk-pulse "$archive/basilisk-pulse"
install -m644 LICENSE docs/LINUX.md packaging/linux/70-basilisk-pulse.rules "$archive/"
tar -czf "release/BasiliskPulse-$version-linux-$machine.tar.gz" -C "$archive" .
# linuxdeploy bundles the Qt runtime and platform plugins into an AppImage.
mkdir -p build/linuxdeploy
for tool in linuxdeploy linuxdeploy-plugin-qt; do
  curl --fail --location --retry 3 "https://github.com/linuxdeploy/$tool/releases/download/continuous/$tool-$machine.AppImage" --output "build/linuxdeploy/$tool-$machine.AppImage"
  chmod +x "build/linuxdeploy/$tool-$machine.AppImage"
done
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$PWD/build/linuxdeploy:$PATH"
export QMAKE=/usr/bin/qmake
export EXTRA_QT_PLUGINS="platforms;imageformats;platformthemes;wayland-graphics-integration-client;wayland-decoration-client"
appdir="$PWD/build/linux-package/AppDir"
mkdir -p "$appdir/usr/share/doc/basilisk-pulse/licenses"
install -m644 LICENSE docs/LINUX.md packaging/linux/70-basilisk-pulse.rules "$appdir/usr/share/doc/basilisk-pulse/"
cp /usr/share/common-licenses/LGPL-3 "$appdir/usr/share/doc/basilisk-pulse/licenses/Qt-LGPL-3.txt"
for copyright in /usr/share/doc/libqt5*/copyright; do
  [[ -f "$copyright" ]] && cp "$copyright" "$appdir/usr/share/doc/basilisk-pulse/licenses/$(basename "$(dirname "$copyright")")-copyright.txt"
done
export OUTPUT="$PWD/release/BasiliskPulse-$version-linux-$machine.AppImage"
"build/linuxdeploy/linuxdeploy-$machine.AppImage" --appdir "$appdir" --executable build/linux/basilisk-pulse --desktop-file packaging/linux/basilisk-pulse.desktop --icon-file packaging/linux/basilisk-pulse.png --plugin qt --output appimage
