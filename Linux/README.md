# OpenDisplay Linux CLI MVP

This target connects the existing, unmodified iOS/iPadOS receiver to a KDE
Wayland session. It discovers `_opensidecar._tcp` services with Avahi or opens
the receiver's port through usbmuxd, creates a virtual output through the KDE
RemoteDesktop/ScreenCast portal, captures it with PipeWire, and streams Annex B
H.264 produced by FFmpeg. There is no desktop GUI.

## Arch Linux dependencies

```sh
sudo pacman -S --needed base-devel cmake qt6-base pipewire avahi \
  libusbmuxd usbmuxd ffmpeg libkscreen \
  xdg-desktop-portal xdg-desktop-portal-kde
```

For hardware encoding, install the driver for the GPU: `libva-mesa-driver` for AMD,
`intel-media-driver` for modern Intel GPUs, or `nvidia-utils` for NVIDIA.
`libva-utils` is useful for checking VA-API with `vainfo`. The process must be
able to open `/dev/dri/renderD128`; normal Arch installations grant this through
the active desktop session.

USB requires the device to be unlocked and paired with the machine. Accept the
device's Trust prompt, then verify the usbmuxd path before starting OpenDisplay:

```sh
idevice_id -l
idevicepair pair       # first connection only
idevicepair validate
```

If `idevice_id` cannot see the device, reconnect the cable and inspect
`journalctl -u usbmuxd.service`; OpenDisplay cannot use a device that usbmuxd
has not finished adding. A journal `lockdown error -8` is a failed mux
preflight: unlock and disconnect the device, restart `usbmuxd.service`, then
reconnect and accept the Trust prompt. Start the service if socket activation
does not do so automatically. Wi-Fi discovery requires a running Avahi daemon;
installing the package does not enable system services:

```sh
sudo systemctl enable --now avahi-daemon.service
systemctl status avahi-daemon.service
```

## Build and test

```sh
cmake -S Linux -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux -j
ctest --test-dir build/linux --output-on-failure
```

## Build and install the Arch package

The `-git` package follows the pushed `linux-port` branch on the `gitea`
remote. From the repository root, use a clean package build so an older clone
of `main` cannot remain in makepkg's `src/` directory:

```sh
(cd Linux/packaging/arch && makepkg --cleanbuild --syncdeps --install)
```

The short equivalent is `makepkg -Csi`. Building the package consumes the
pushed branch, not uncommitted files in the current worktree. Push a commit to
`gitea/linux-port` before packaging when testing new local changes.

## Run

Open the existing OpenDisplay app on the iOS device, then run:

```sh
./build/linux/opendisplay-linux --transport auto --mode extend
```

The first run displays KDE portal dialogs for output capture and pointer
control. Other useful forms are:

```sh
./build/linux/opendisplay-linux --list
./build/linux/opendisplay-linux --transport usb --udid DEVICE_UDID
./build/linux/opendisplay-linux --transport wifi --host 192.168.1.40
./build/linux/opendisplay-linux --encoder vaapi --vaapi-device /dev/dri/renderD128
./build/linux/opendisplay-linux --encoder nvenc --mode mirror --no-input
```

### Virtual monitor layout

In extend mode, OpenDisplay queries `kscreen-doctor -j` before opening the KDE
portal. One enabled monitor is selected automatically; with two or more, pass
its connector name or KScreen id explicitly:

```sh
opendisplay-linux --reference-monitor DP-1
opendisplay-linux --reference-monitor eDP-1 --extend-to right --align-to bottom
opendisplay-linux --extend-to bottom --align-to right
```

The default is bottom-right: extend right and align bottom. Left/right extension
accepts `top`, `bottom`, or `center` alignment; top/bottom extension accepts
`left`, `right`, or `center`. Positions use KDE logical coordinates.

The automatic mode starts with the iPad's native pixels. It uses physical DPI
when both panel sizes are available, otherwise the iOS-reported native scale;
the result is rounded to a 0.05 scale step. Pixel dimensions are nudged to the
nearest even values that produce integer logical dimensions. Detection and any
part of the calculation can be overridden:

```sh
opendisplay-linux --virtual-resolution 2420x1668 --display-scale 1.25
opendisplay-linux --virtual-refresh 60 --ipad-size-mm 263x181
opendisplay-linux --reference-resolution 3840x2160 --reference-scale 1.5 \
  --reference-size-mm 597x336
opendisplay-linux --reference-geometry 2560x1440+0+0
```

`--scale` remains the video encoder resolution multiplier and does not change
KDE display scaling. Custom virtual modes require Plasma/libkscreen 6.6 or
newer; `libkscreen` supplies `kscreen-doctor`.

If Bonjour discovery is unavailable, confirm that the phone app is open and
inspect the advertised service with `avahi-browse -rt _opensidecar._tcp`.
You can bypass Avahi when the receiver's address is known:

```sh
opendisplay-linux --transport wifi --host 192.168.1.40 --port 9000
```

`--encoder auto` prefers VA-API, then NVENC, and falls back to `libx264`.
Use `Linux/tools/fake_receiver.py` to exercise the TCP framing without an iOS
device; it does not decode video or advertise Bonjour.

## Current MVP limits

KDE Plasma Wayland is the only supported desktop. Portal support determines
whether true virtual-output extension is available; `--mode mirror` is the
fallback on systems whose portal cannot create a virtual source. Audio,
clipboard, cursor sprites, encryption, reconnection, and multi-device sessions
remain out of scope. Compositor-neutral layout lives in `display_layout`, while
KScreen operations are isolated in `KdeOutputController` and portal capture in
`KdePortal`. Later wlroots controllers need not change transport, protocol,
layout planning, or encoding code.
