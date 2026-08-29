<div align="center">

<img src="public/logo.png" width="128" alt="OpenDisplay app icon" />

# OpenDisplay Linux Fork

**Use an iPhone or iPad as an additional display on a Linux Wayland desktop.**

[Install the iOS/iPadOS receiver](https://peetzweg.github.io/opendisplay/) · [Linux guide](Linux/README.md) · [Upstream project](https://github.com/peetzweg/opendisplay)

</div>

> [!WARNING]
> This is a highly experimental, vibe-coded personal fork. It is not expected
> to merge back into the upstream project, compatibility may break without
> notice, and maintenance will be limited. Use it only if you are comfortable
> diagnosing Linux, Wayland, PipeWire, portal, and hardware-encoding issues.

## Current scope

This fork adds a Linux sender while retaining the original OpenDisplay
iOS/iPadOS receiver and wire protocol. The Linux application supports:

- KDE Plasma Wayland through `xdg-desktop-portal-kde` and KScreen.
- Hyprland through `xdg-desktop-portal-hyprland` and headless outputs.
- Wi-Fi discovery with Avahi and USB connectivity through `usbmuxd`.
- PipeWire capture and FFmpeg H.264 encoding, including supported hardware
  encoders.
- A command-line client and an experimental Qt Quick/Kirigami GUI.

Other compositors and non-Wayland sessions are currently unsupported. Portal
behavior also depends on compositor and desktop versions, so successful setup
on one system does not guarantee identical behavior elsewhere.

## Receiver application

No modified mobile application is required. Install and run the same upstream
OpenDisplay receiver on an iPhone or iPad from the
[OpenDisplay installation page](https://peetzweg.github.io/opendisplay/).
The receiver currently requires iOS/iPadOS 16 or newer. The public
[TestFlight beta](https://testflight.apple.com/join/3NYaY11c) remains available
when offered by the upstream project.

## Linux installation and usage

OpenDisplay for Linux requires **Qt6** (Qt 6.5+), which is pulled in
automatically by the package managers below.

**Flatpak (recommended, no Qt needed).** The GUI ships as a self-contained
Flatpak that bundles its own Qt6 — grab the `.flatpak` bundle from the
[releases](https://github.com/TommyBoiss/opendisplay-linux/releases) page:

```sh
flatpak install --user org.opendisplay.desktop-*.flatpak
flatpak run org.opendisplay.desktop
```

**Fedora binaries.** Prebuilt binaries are attached to the
[releases](https://github.com/TommyBoiss/opendisplay-linux/releases) page — no
compiling needed. Just install the runtime dependencies and run:

```sh
sudo dnf install qt6-qtbase qt6-qtdeclarative qt6-qtquickcontrols2 qt6-qtwayland \
  kf6-kirigami libkscreen pipewire \
  xdg-desktop-portal xdg-desktop-portal-kde
```

To build from source on Fedora, run the setup script to download everything
(build and runtime deps plus RPM Fusion for `libx264`):

```sh
Linux/packaging/fedora/setup.sh
Linux/packaging/fedora/build-fedora.sh
```

On Arch Linux, an `opendisplay-linux-git` `PKGBUILD` is provided under
[`Linux/packaging/arch/`](Linux/packaging/arch/PKGBUILD). Source builds and the
package include both `opendisplay-linux` and `opendisplay-gui`.

For package-building commands, Wi-Fi and USB setup, monitor placement and
scaling options, KDE/Hyprland notes, and troubleshooting, use the dedicated
[Linux README](Linux/README.md).

## Upstream code and license

The original macOS sender, iOS/iPadOS receiver, website, and shared protocol
remain in this repository for compatibility and reference. Upstream development
belongs at [peetzweg/opendisplay](https://github.com/peetzweg/opendisplay).
This fork remains available under the repository's [GPL-3.0 license](LICENSE).
