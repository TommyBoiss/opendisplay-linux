# OpenDisplay Linux CLI MVP

This target connects the existing, unmodified iOS/iPadOS receiver to a KDE
Wayland session. It discovers `_opensidecar._tcp` services with Avahi or opens
the receiver's port through usbmuxd, creates a virtual output through the KDE
RemoteDesktop/ScreenCast portal, captures it with PipeWire, and streams Annex B
H.264 produced by FFmpeg. There is no desktop GUI.

## Arch Linux dependencies

```sh
sudo pacman -S --needed base-devel cmake qt6-base pipewire avahi \
  libusbmuxd usbmuxd ffmpeg xdg-desktop-portal xdg-desktop-portal-kde
```

For hardware encoding, install the driver for the GPU: `libva-mesa-driver` for AMD,
`intel-media-driver` for modern Intel GPUs, or `nvidia-utils` for NVIDIA.
`libva-utils` is useful for checking VA-API with `vainfo`. The process must be
able to open `/dev/dri/renderD128`; normal Arch installations grant this through
the active desktop session.

USB requires the device to be paired with the machine. Start `usbmuxd.service`
if socket activation does not do so automatically. Wi-Fi discovery requires
`avahi-daemon.service`.

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

`--encoder auto` prefers VA-API, then NVENC, and falls back to `libx264`.
Use `Linux/tools/fake_receiver.py` to exercise the TCP framing without an iOS
device; it does not decode video or advertise Bonjour.

## Current MVP limits

KDE Plasma Wayland is the only supported desktop. Portal support determines
whether true virtual-output extension is available; `--mode mirror` is the
fallback on systems whose portal cannot create a virtual source. Audio,
clipboard, cursor sprites, encryption, reconnection, and multi-device sessions
remain out of scope. The platform boundary is isolated in `KdePortal`, so later
wlroots and other compositor implementations need not change transport,
protocol, or encoding code.
