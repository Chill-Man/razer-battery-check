# Linux preview

Basilisk Pulse has a native Qt 5 desktop frontend on Linux and the same C++ battery model, discharge regression, cache validation and recovery state as the Windows app. It does not run the Windows executable through Wine.

**Status:** native builds, unit tests and GUI rendering are checked in CI. USB telemetry has not yet been tested on physical Linux hardware for this project. The 35K family also remains hardware-unverified. Linux packages are a preview; report device-specific results with `--probe`.

## Choose a package

| Architecture | Package baseline | Formats |
| --- | --- | --- |
| `x86_64` / `amd64` | Ubuntu 22.04 or newer; glibc 2.35+ | AppImage, DEB, tar.gz |
| `aarch64` / `arm64` | Ubuntu 24.04 or newer; glibc 2.39+ | AppImage, DEB, tar.gz |

These are build baselines, not a claim that every distribution or desktop has been tested. Other distributions need compatible libc / C++ libraries. The DEB package is for compatible Debian-family systems. 32-bit Linux is not currently packaged.

## AppImage

```sh
chmod +x BasiliskPulse-*-linux-x86_64.AppImage
./BasiliskPulse-*-linux-x86_64.AppImage --show
```

Choose the `aarch64` file on ARM64. The AppImage bundles Qt. On systems without FUSE, run it with `--appimage-extract-and-run` or extract it and run `squashfs-root/AppRun`.

Keep the AppImage in a stable location before enabling autostart. The app records the original AppImage path, not its temporary mount directory.

## DEB

```sh
sudo apt install ./BasiliskPulse-*-linux-x86_64.deb
basilisk-pulse --show
```

The package installs a desktop launcher, icon and a device access rule. It does not automatically enable autostart. Use Settings to opt in. To uninstall:

```sh
sudo apt remove basilisk-pulse
```

## Archive

Extract the `.tar.gz`, install the distribution's Qt 5 Widgets and Network runtime libraries, then run `./basilisk-pulse --show`. On Ubuntu, the relevant packages are `libqt5widgets5`, `libqt5network5`, and optionally `qtwayland5` for Wayland. Newer Ubuntu releases may use `t64` runtime package names; install the distribution's matching Qt 5 packages.

The archive does not bundle Qt and is not a fully self-contained executable. It contains the app, MIT license, this guide and the udev rule.

## Allow access to the mouse

Reading the supported USB mouse requires permission to access its `/dev/hidraw*` node. **Run the application as your ordinary user.** Do not run the whole GUI as root.

The DEB installs the rule automatically. For the AppImage/archive, install the bundled rule once:

```sh
sudo install -m 0644 70-basilisk-pulse.rules /etc/udev/rules.d/70-basilisk-pulse.rules
sudo udevadm control --reload-rules
```

Reconnect the mouse receiver after installing the rule. For the AppImage, the rule is inside `squashfs-root/usr/share/doc/basilisk-pulse/` after `--appimage-extract`.

The rule uses logind's `uaccess` tag for six supported Razer USB IDs. It does not make every HID device world-writable or require membership in the system-wide `input` group. Sessions without logind/uaccess may need a distribution-specific device permission configuration.

## Tray and desktop behavior

- Tray support depends on the desktop's XEmbed / StatusNotifier / AppIndicator implementation.
- GNOME Shell may need an AppIndicator-compatible extension to show tray icons.
- A left click opens the compact panel where supported. A right click opens a menu containing battery percentage, runtime, Refresh and Open panel.
- If no tray is available, the app opens a standalone panel so it remains accessible.
- Window positioning can vary on Wayland. Use the application launcher or `--show` to reopen the panel.
- Linux has English interface labels. Mouse settings are never changed.

## Local data

History and last-reading cache live under `$XDG_DATA_HOME/BasiliskPulse`, or `~/.local/share/BasiliskPulse` when XDG_DATA_HOME is not set. Preferences are in Qt's user config directory for `BasiliskPulse`; autostart uses `~/.config/autostart/basilisk-pulse.desktop` (respecting XDG_CONFIG_HOME).

Normal polling uses 2 or 5 minutes. Recovery retries after 2, 5, 15, 30 and 60 seconds. Opening the panel, Refresh and `/dev` directory changes trigger checks. Linux does not install global input hooks to track typing or mouse movement.

## Diagnostics and source build

```sh
./basilisk-pulse --version
./basilisk-pulse --probe
```

For a source build on Ubuntu:

```sh
sudo apt install build-essential cmake qtbase5-dev
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux --parallel
ctest --test-dir build/linux --output-on-failure
./build/linux/basilisk-pulse --show
```

AppImages include LGPL-3 license text and Qt package copyright notices under `usr/share/doc/basilisk-pulse/licenses`. Qt is dynamically linked and can be replaced after extracting the AppImage. Qt source is available from [Qt](https://code.qt.io/cgit/qt/qtbase.git/) and the corresponding [Ubuntu Qt source packages](https://launchpad.net/ubuntu/+source/qtbase-opensource-src). Product imagery and trademarks retain their owners' rights.
