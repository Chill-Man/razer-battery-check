## Windows

- Native **x86 (32-bit), x64 and ARM64** builds.
- Each architecture has a portable ZIP and a per-user Setup installer.
- Static C++ runtime: no separate .NET or Visual C++ runtime installation.
- Existing history and settings survive updates.

## Linux preview

- Native **x86_64 and aarch64** Qt desktop builds.
- **AppImage, DEB and tar.gz** packages for each architecture.
- Shared battery model, cached readings, discharge history and recovery logic.
- The AppImage bundles Qt; the tar archive uses system Qt libraries.
- [Linux installation and device permissions](https://github.com/Chill-Man/razer-battery-check/blob/main/docs/LINUX.md).

**Validation:** native regression tests and package smoke tests run on all five CPU/OS targets. Windows x86 and x64 are also built and tested locally. Linux GUI rendering is checked in a virtual display. Physical Linux HID telemetry and physical 35K hardware remain unverified; Linux packages are a preview.

**Choose x64 on most Intel/AMD Windows PCs, ARM64 on Snapdragon Windows PCs, or x86 for 32-bit Windows 10.** For Linux, use `uname -m` to choose the matching package.

`SHA256SUMS.txt` contains checksums for every downloadable build. The app is unsigned. MIT applies to project software; product imagery and bundled third-party libraries retain their respective licenses.
