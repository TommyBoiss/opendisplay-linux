# OpenDisplay Flatpak

Builds the OpenDisplay GUI (`opendisplay-gui`) as a self-contained Flatpak that
bundles its Qt6/KDE runtime, so there is no host Qt version-mismatch problem.

The GUI can act as a **sender** (use an iPad as a display) or a **receiver**
(act as a display for a Mac or another Linux machine, forwarding input back).

## Build

```sh
sudo dnf install flatpak flatpak-builder
flatpak remote-add --if-not-exists flathub \
  https://flathub.org/repo/flathub.flatpakrepo
flatpak install flathub org.kde.Platform//6.10 org.kde.Sdk//6.10

./Linux/packaging/flatpak/build-flatpak.sh
```

Install and run the resulting bundle:

```sh
flatpak install --user build/org.opendisplay.desktop-6.10.flatpak
flatpak run org.opendisplay.desktop
```

## Receiver mode

Choose **Receive** in the Role dropdown. The app advertises itself as an
`_opensidecar._tcp` service, listens on the configured port (default 9000),
and displays the incoming H.264 stream. Mouse/touch and wheel input are
forwarded back to the sender. The advertised resolution follows the video
window; resizing re-negotiates with the sender.

## What it bundles

| Module | Version | License | Why |
|--------|---------|---------|-----|
| OpenDisplay GUI | repo | GPL-3.0-only | the app itself |
| avahi | v0.8 | LGPL-2.1-or-later | `_opensidecar._tcp` mDNS discovery + advertisement |
| libplist | 2.7.0 | LGPL-2.1 | USB transport support |
| libimobiledevice-glue | 1.3.2 | LGPL-2.1 | USB transport support |
| libusbmuxd | 2.1.1 | LGPL-2.1 | USB transport support |
| libkscreen | 6.6.0 | LGPL-2.1-or-later | KDE screen management (not in the KDE SDK) |

Qt6, KDE Frameworks (Kirigami), PipeWire, FFmpeg and xdg portals are pulled
from the `org.kde.Platform//6.10` runtime rather than being bundled, so their
licenses live with the runtime itself.

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
  - `libkscreen/LICENSES/` (KDE REUSE layout)
- All bundled libraries are dynamically linked.
- The LGPL libraries (avahi, libplist, libimobiledevice-glue, libusbmuxd,
  libkscreen) permit dynamic linking from a GPL application, and the app's
  source is available under this repository, so the GPL's
  source-corresponding-source obligations are met on the
  [GitHub Releases](https://github.com/TommyBoiss/opendisplay-linux/releases)
  and source tree.

## Sandbox notes

- `--share=network` is needed for mDNS discovery and streaming over Wi-Fi.
- `--device=dri` grants hardware encoding via VA-API.
- Screen capture and input go through `xdg-desktop-portal` (
  `--talk-name=org.freedesktop.portal.*`).
- The system tray (StatusNotifier) needs `--talk-name=org.kde.StatusNotifierWatcher`.
- mDNS discovery needs `--system-talk-name=org.freedesktop.Avahi` (avahi-daemon
  runs on the host's system bus).
- USB transport needs `--filesystem=/var/run/usbmuxd`.

## Known limitations inside the sandbox

- **Hyprland backend is best-effort.** The code shells out to `hyprctl`, which
  the sandbox does not provide, and the compositor socket lives under the host's
  `XDG_RUNTIME_DIR`. KDE/Plasma is the supported path inside the Flatpak.
- **VA-API encode is unverified** in the sandbox; if `h264_vaapi` is unavailable
  the encoder falls back to software `libx264`.
