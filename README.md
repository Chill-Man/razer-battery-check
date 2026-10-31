<div align="center">

<img src="src/mouse.png" alt="Razer Basilisk mouse" height="100">

# Basilisk Pulse

### Your mouse battery. One glance away.

A lightweight, native Windows tray app for **Razer Basilisk V3 Pro** and **Basilisk V3 Pro 35K**.
Battery percentage, an estimate of time remaining to 1%, and a compact interface that stays out of your way.

[![Downloads](https://img.shields.io/github/downloads/Chill-Man/razer-battery-check/total?style=for-the-badge&color=a5f57a&label=Downloads)](https://github.com/Chill-Man/razer-battery-check/releases)
[![Latest release](https://img.shields.io/github/v/release/Chill-Man/razer-battery-check?style=for-the-badge&color=9ac9b0)](https://github.com/Chill-Man/razer-battery-check/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-dce8df?style=for-the-badge)](LICENSE)
![Platform](https://img.shields.io/badge/Windows-10%20%2F%2011-303e35?style=for-the-badge&logo=windows)
![Native C++](https://img.shields.io/badge/Native-C%2B%2B17-303e35?style=for-the-badge&logo=cplusplus)

**[Releases](https://github.com/Chill-Man/razer-battery-check/releases)** · **[Build from source](#build-from-source)** · **[Report an issue](https://github.com/Chill-Man/razer-battery-check/issues)** · **[Русский](docs/README.ru.md)**

</div>

---

## Small app. Useful details.

Keep your battery percentage in the system tray and open the panel with a click. See the last confirmed charge, whether the mouse is charging, and how long it might last before reaching 1%.

Basilisk Pulse reads telemetry directly through Windows HID APIs. It does not require a running Razer Synapse interface, an account, a browser engine, or a background web server. Mouse settings stay untouched.

| At a glance | Under the hood |
| :--- | :--- |
| **Battery in the tray** — readable percentage without opening a window. | **Native C++ / Win32** — no Electron, WebView, or .NET runtime dependency. |
| **Time remaining** — starts with a configurable battery-life profile, then learns from discharge history. | **Direct HID telemetry** — battery and charging queries only. |
| **Compact panel** — 320 × 344 logical pixels, rounded surfaces and clear typography. | **Resilient polling** — bounded retries, cached readings, and automatic recovery. |
| **Quiet motion** — soft ambient light and smooth switch transitions. | **Local storage** — no analytics, cloud sync, or network requests. |
| **Optional autostart** — available in Settings. | **Motion on demand** — animation timers stop when the panel is hidden. |

## Get started

### Portable

Prebuilt downloads, when published, are available on the **[Releases page](https://github.com/Chill-Man/razer-battery-check/releases)**. Run `BasiliskPulse.exe` from a folder you intend to keep. If no release is available yet, [build it locally](#build-from-source).

1. Connect your supported mouse through its HyperSpeed receiver or USB cable.
2. Launch the app. Check the **hidden tray icons** near the Windows clock if the percentage is not immediately visible.
3. Left-click or right-click the percentage to open the panel.

Click outside the panel or press **Esc** to hide it. Choose **Exit** to stop the application.

### Install for your Windows account

After building the app, run this from the project folder:

```powershell
.\install.ps1
```

The installer copies the executable to `%LOCALAPPDATA%\Programs\BasiliskPulse` and creates a **Basilisk Pulse** shortcut in the Start menu. Administrator privileges are not required. Enable **Start with Windows** in the app if desired.

To remove the installed copy:

```powershell
.\uninstall.ps1
```

Uninstallation removes the application, shortcut, and autostart entry. Your measurement history and preferences are retained.

## Supported mice

| Model | USB cable | HyperSpeed receiver | Initial battery-life profile |
| :--- | :---: | :---: | :---: |
| Basilisk V3 Pro | `1532:00AA` | `1532:00AB` | 110 hours |
| Basilisk V3 Pro 35K | `1532:00CC` | `1532:00CD` | 140 hours |
| Basilisk V3 Pro 35K Phantom Green | `1532:00D6` | `1532:00D7` | 140 hours |

**Hardware verification:** direct telemetry has been tested on the original Basilisk V3 Pro receiver (`1532:00AB`). The 35K variants are implemented using documented community protocol identifiers and have not yet been verified on physical 35K hardware in this project.

Bluetooth and separate Mouse Dock Pro / HyperPolling receiver PIDs are not currently supported. The wired **Basilisk V3 35K without “Pro”** is a different device and has no battery to monitor.

## How the estimate works

The displayed runtime is an **estimate**, not a countdown or a guarantee of when the mouse will turn off.

**Before enough history exists**, the app uses:

```text
remaining hours = max(battery percentage − 1, 0) × full-charge profile / 100
```

The initial profiles use manufacturer battery-life figures. RGB lighting, polling rate, battery age, firmware and your usage can change the result. Adjust the full-charge profile to match your experience.

**After a useful discharge segment is recorded**, the app fits a linear regression to the measurements. It needs at least 30 minutes, four samples, and a drop of at least 8 out of 255 raw battery units — approximately 3%.

Charging, a rise in battery level, or a measurement gap longer than 10 minutes starts a new segment. The estimate includes the time between readings, including short idle periods. Histories and custom profiles are kept separate for the original Pro and the 35K family.

| Profile | Original Pro | Pro 35K |
| :--- | :---: | :---: |
| Standard HyperSpeed reference | 110 h | 140 h |
| 8000 Hz reference | 32 h | 28 h |
| Intermediate custom preset | 60 h | 60 h |

These reference values are not measurements of your current configuration. See the official [V3 Pro specifications](https://mysupport.razer.com/app/answers/detail/a_id/6219) and [V3 Pro 35K specifications](https://www.razer.com/gaming-mice/razer-basilisk-v3-pro-35k).

## A sleeping mouse should not erase its battery

Brief HID failures keep the last confirmed percentage and estimate visible. Failed requests are retried after **2, 5, 15, 30, and 60 seconds**, then fall back to your configured polling interval.

During recovery, new mouse or keyboard activity can trigger another attempt within five seconds. Opening the panel, device changes, and resuming Windows also request a refresh.

If fresh readings remain unavailable, the panel marks the value as **last known**. Hover over the tray icon for the age of that reading. The cached value survives an app restart; it is never presented as a newly confirmed hardware response.

## Designed to stay lightweight

- Normal polling: **every 2 minutes**. Economy mode: **every 5 minutes**.
- No raw-input hooks, browser processes, or permanent rendering loop in the tray.
- Ambient animation redraws only the region around the mouse, up to 30 frames per second while the main panel is visible.
- Toggle transitions last **240 ms**, ease smoothly to their target, and reverse from their current position if clicked again.
- Animation timers stop when hidden or when they finish. Windows reduced-motion settings are respected.
- Graphics resources and font caches are released when the panel closes.
- A low-battery notification appears at 15%, once per discharge cycle.

## Local data and privacy

**The application does not send network requests.** All measurements stay on your PC.

| Location | Contents |
| :--- | :--- |
| `%LOCALAPPDATA%\BasiliskPulse\history.tsv` | Original Pro discharge history. |
| `%LOCALAPPDATA%\BasiliskPulse\history-35k.tsv` | 35K discharge history. |
| `%LOCALAPPDATA%\BasiliskPulse\last-reading.tsv` | Last confirmed reading, saved atomically. |
| `%LOCALAPPDATA%\BasiliskPulse\status.json` | Current diagnostic snapshot and retry state. |
| `HKCU\Software\BasiliskPulse` | App preferences and per-model profiles. |

History is bounded to **30 days / 15,000 samples per model**. Cached and failed readings are excluded from discharge training.

## Build from source

Requires **Windows x64**, **Visual Studio Build Tools with Desktop development with C++**, and a **Windows 10/11 SDK**. No package manager or third-party C++ dependencies are required.

```powershell
git clone https://github.com/Chill-Man/razer-battery-check.git
cd razer-battery-check
.\build.ps1
```

The script builds the executable with a static C runtime and runs the regression tests.

```text
dist\BasiliskPulse.exe    Portable application
build\tests.exe           Test executable
```

Useful commands:

```powershell
# Print HID interfaces and try a real battery query
.\build\tests.exe --probe

# Open an inspection panel that stays visible after losing focus
.\dist\BasiliskPulse.exe --inspect

# Show guides for charge-number / mouse-image alignment
.\dist\BasiliskPulse.exe --inspect --alignment-guides
```

Tests cover discharge regression, quantized readings, charging, gaps, level increases, the 1% threshold, HID reply validation, supported device IDs, model isolation, transient failures, recovery, persistent cache restoration, corrupted data, and a valid 0% reading.

## Troubleshooting

**Cannot find the app?** Check the hidden tray icons next to the clock. Windows decides where new notification icons appear.

**The value is marked last known?** Move the physical mouse to wake it, check the receiver, and use **Refresh**. The app retries automatically. A cached percentage is kept visible while waiting.

**The runtime seems optimistic?** Reduce the full-charge profile in Settings and let the app collect a continuous discharge segment. High polling rates and RGB can reduce battery life substantially.

**A supported 35K does not respond?** Open an [issue](https://github.com/Chill-Man/razer-battery-check/issues) with the Windows version, mouse variant, connection type, firmware version if known, and the relevant `--probe` output. Remove device serial numbers or other personal information before sharing logs.

## Downloads badge

The badge at the top uses GitHub's **release asset download counts** and updates automatically. It does not count clones, repository ZIP downloads, or unique users. Until release assets are published, a download total may be unavailable.

## Credits and license

Created by **[Chill-Man](https://github.com/Chill-Man)**. Software is released under the **[MIT License](LICENSE)**.

Protocol references: [OpenRazer command definitions](https://github.com/openrazer/openrazer/blob/master/driver/razerchromacommon.c) and [device identifiers](https://github.com/openrazer/openrazer/blob/master/driver/razermouse_driver.h). The Windows implementation in this repository was written independently.

Razer product imagery and trademarks belong to their respective owners and are not relicensed under MIT. This is an independent community project, not an official Razer product.
