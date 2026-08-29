#!/usr/bin/env bash
# Build and install OpenDisplay for Linux on Fedora.
#
# Usage:
#   ./build-fedora.sh            # configure + build in build/linux
#   ./build-fedora.sh install    # also install to /usr/local
#   ./build-fedora.sh rpm        # build an RPM package
#
# Run from the repository root.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD_DIR="${ROOT}/build/linux"

check_deps() {
  local missing=()
  for pkg in cmake gcc-c++ ninja-build qt6-qtbase-devel qt6-qtdeclarative-devel \
      qt6-qtwayland-devel \
      kf6-kirigami-devel libkscreen-devel pipewire-devel avahi-devel \
      libusbmuxd-devel wayland-devel \
      ffmpeg-devel ffmpeg-free-devel ffmpeg; do
    if ! rpm -q "$pkg" >/dev/null 2>&1; then
      missing+=("$pkg")
    fi
  done
  if [[ ${#missing[@]} -gt 0 ]]; then
    echo "Missing build dependencies:" >&2
    printf '  %s\n' "${missing[@]}" >&2
    echo "Install them with:" >&2
    echo "  Linux/packaging/fedora/setup.sh" >&2
    echo "  # or manually:" >&2
    echo "  sudo dnf install ${missing[*]}" >&2
    exit 1
  fi
}

build() {
  cmake -S "${ROOT}/Linux" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DOPENDISPLAY_BUILD_GUI=ON \
    -DBUILD_TESTING=ON
  cmake --build "${BUILD_DIR}" --parallel
  ctest --test-dir "${BUILD_DIR}" --output-on-failure
}

case "${1:-build}" in
  build)
    check_deps
    build
    echo
    echo "Build complete. Run:"
    echo "  ${BUILD_DIR}/opendisplay-gui"
    echo "  ${BUILD_DIR}/opendisplay-linux --transport auto --mode extend"
    ;;
  install)
    check_deps
    build
    sudo cmake --install "${BUILD_DIR}"
    ;;
  rpm)
    if ! command -v rpmbuild >/dev/null 2>&1; then
      echo "rpmbuild not found. Install with: sudo dnf install rpm-build" >&2
      exit 1
    fi
    SPEC="${ROOT}/Linux/packaging/fedora/opendisplay-linux.spec"
    rpmbuild -ba "$SPEC"
    ;;
  *)
    echo "Unknown command: $1" >&2
    echo "Usage: $0 [build|install|rpm]" >&2
    exit 1
    ;;
esac
