# OpenDisplay for Linux — Fedora RPM spec
#
# Build with:
#   rpmbuild -ba opendisplay-linux.spec
# or use the helper script in this directory:
#   ./build-rpm.sh

%global __cmake_in_source_build 1

Name:           opendisplay-linux
Version:        0.1.0
Release:        1%{?dist}
Summary:        Use an iOS device as a KDE or Hyprland Wayland display

License:        GPL-3.0-only
URL:            https://github.com/tixwho/opendisplay-linux
Source0:        %{url}/archive/refs/heads/linux-port.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  ninja-build
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  qt6-qtwayland-devel
BuildRequires:  kf6-kirigami-devel
BuildRequires:  libkscreen-devel
BuildRequires:  pipewire-devel
BuildRequires:  avahi-devel
BuildRequires:  libusbmuxd-devel
BuildRequires:  wayland-devel
BuildRequires:  ffmpeg-devel
BuildRequires:  pkgconfig

Requires:       qt6-qtbase
Requires:       qt6-qtdeclarative
Requires:       qt6-qtwayland
Requires:       kf6-kirigami
Requires:       libkscreen
Requires:       pipewire
Requires:       avahi
Requires:       libusbmuxd
Requires:       usbmuxd
Requires:       ffmpeg
Requires:       xdg-desktop-portal
Requires:       xdg-desktop-portal-kde

%description
OpenDisplay connects an existing, unmodified iOS/iPadOS receiver to a KDE or
Hyprland Wayland session. It discovers _opensidecar._tcp services with Avahi or
opens the receiver's port through usbmuxd, captures with PipeWire, and streams
Annex B H.264 produced by FFmpeg. The command-line client and an initial
Kirigami control application share the same connection engine.

%prep
%autosetup -n opendisplay-linux-linux-port

%build
%cmake -S Linux -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DOPENDISPLAY_BUILD_GUI=ON \
    -DBUILD_TESTING=ON
%cmake_build

%check
ctest --test-dir build --output-on-failure

%install
%cmake_install

%files
%{_bindir}/opendisplay-linux
%{_bindir}/opendisplay-gui
%{_datadir}/applications/org.opendisplay.desktop.desktop
%{_datadir}/icons/hicolor/256x256/apps/org.opendisplay.desktop.png
%doc README.md

%changelog
* Fri Aug 29 2026 OpenDisplay Contributors - 0.1.0-1
- Initial Fedora packaging
