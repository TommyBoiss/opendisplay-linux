# OpenDisplay Flatpak

Builds the OpenDisplay GUI (`opendisplay-gui`) as a self-contained Flatpak that
bundles its Qt6/KDE runtime, so there is no host Qt version-mismatch problem.

## Build

```sh
sudo dnf install flatpak flatpak-builder
flatpak remote-add --if-not-exists flathub \
  https://flathub.org/repo/flathub.flatpakrepo
flatpak install flathub org.kde.Platform//6.7 org.kde.Sdk//6.7

./Linux/packaging/flatpak/build-flatpak.sh
```

Install and run the resulting bundle:

```sh
flatpak install --user build/org.opendisplay.desktop-6.7.flatpak
flatpak run org.opendisplay.desktop
```

## What it bundles

| Module | Version | License | Why |
|--------|---------|---------|-----|
| OpenDisplay GUI | repo | GPL-3.0-only | the app itself |
| avahi | v0.8 | LGPL-2.1-or-later | `_opensidecar._tcp` mDNS discovery |
| libplist | 2.3.0 | LGPL-2.1 | USB transport support |
| libimobiledevice-glue | 1.2.0 | LGPL-2.1 | USB transport support |
| libusbmuxd | 2.0.2 | LGPL-2.1 | USB transport support |

Qt6, KDE Frameworks (Kirigami, libkscreen), PipeWire, FFmpeg and xdg portals
are pulled from the `org.kde.Platform//6.7` runtime rather than being bundled,
so their licenses live with the runtime itself.

## License compliance

- The app is **GPL-3.0-only** and is installed under `LICENSE` the same way the
  source provides it.
- Each bundled library's license text is installed into
  `/app/share/licenses/<module>/` inside the built app, satisfying the "give a
  copy of the license" requirement of the LGPL:
  - `avahi/LICENSE`
  - `libplist/COPYING`
  - `libimobiledevice-glue/COPYING`
  - `libusbmuxd/COPYING`
- All bundled libraries are dynamically linked.
- The LGPL libraries (avahi, libplist, libimobiledevice-glue, libusbmuxd)
  permit dynamic linking from a GPL application, and the app's source is
  available under this repository, so the GPL's source-corresponding-source
  obligations are met on the [GitHub Releases](https://github.com/TommyBoiss/opendisplay-linux/releases)
  and source tree.

## Sandbox notes

- `--share=network` is needed for mDNS discovery and streaming over Wi-Fi.
- `--device=dri` grants hardware encoding via VA-API.
- Screen capture and input go through `xdg-desktop-portal` (
  `--talk-name=org.freedesktop.portal.*`).
- `--filesystem=/tmp` lets the **Hyprland** backend reach `hyprctl`'s host
  socket. If you only use Plasma/KDE, remove that line for a tighter sandbox.
