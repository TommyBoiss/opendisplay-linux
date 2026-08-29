#!/usr/bin/env bash
# Install OpenDisplay for Linux build/runtime dependencies on Fedora.
#
# Usage:
#   ./setup.sh                # enable RPM Fusion, install everything
#   ./setup.sh --no-rpmfusion # skip RPM Fusion (free ffmpeg only)
#   ./setup.sh --help
#
# Note: RPM Fusion provides the `ffmpeg` package with libx264 used by the
# encoder test. You can skip it to stay with Fedora's ffmpeg-free, but the
# encoder test will fail.

set -euo pipefail

INSTALL_RPMFUSION=1

if [[ $# -gt 0 ]]; then
  case "$1" in
    --no-rpmfusion) INSTALL_RPMFUSION=0 ;;
    --help|-h)
      echo "Usage: $0 [--no-rpmfusion]"
      echo "  --no-rpmfusion   Skip enabling RPM Fusion (uses ffmpeg-free)"
      exit 0
      ;;
    *) echo "Unknown option: $1" >&2; echo "Try: $0 --help" >&2; exit 1 ;;
  esac
fi

if ! command -v dnf >/dev/null 2>&1; then
  echo "This setup script is for Fedora (requires dnf)." >&2
  exit 1
fi

if [[ "$INSTALL_RPMFUSION" -eq 1 ]]; then
  echo "==> Enabling RPM Fusion free repository"
  sudo dnf install -y \
    "https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm"
  FFMPEG="ffmpeg ffmpeg-devel"
else
  echo "==> Skipping RPM Fusion; using Fedora's ffmpeg-free"
  FFMPEG="ffmpeg-free ffmpeg-free-devel"
fi

echo "==> Installing build and runtime dependencies"
sudo dnf install -y \
  cmake gcc-c++ ninja-build \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtwayland-devel \
  qt6-qtmultimedia-devel \
  kf6-kirigami-devel libkscreen-devel \
  pipewire-devel avahi-devel libusbmuxd-devel wayland-devel \
  $FFMPEG \
  xdg-desktop-portal xdg-desktop-portal-kde

echo
echo "==> Done. You can now build with:"
echo "    Linux/packaging/fedora/build-fedora.sh"
