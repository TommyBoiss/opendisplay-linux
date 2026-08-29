#!/usr/bin/env bash
# Build the OpenDisplay GUI as a Flatpak using flatpak-builder.
#
# Requirements on the host (Fedora):
#   sudo dnf install flatpak flatpak-builder
#   flatpak remote-add --if-not-exists flathub \
#     https://flathub.org/repo/flathub.flatpakrepo
#   flatpak install flathub org.kde.Platform//6.9 org.kde.Sdk//6.9
#
# Usage (run from the repository root):
#   Linux/packaging/flatpak/build-flatpak.sh

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT}/build/flatpak"
REPO_DIR="${ROOT}/build/flatpak-repo"
RUNTIME_VERSION="6.9"
APP_ID="org.opendisplay.desktop"

# flatpak-builder runs module sources relative to the manifest's directory.
cd "$DIR"

flatpak-builder \
  --repo="${REPO_DIR}" \
  --ccache \
  --install-deps-from=flathub \
  --state-dir="${BUILD_DIR}/.flatpak-builder" \
  --force-clean \
  --default-branch "${RUNTIME_VERSION}" \
  "${BUILD_DIR}" \
  "${APP_ID}.yml"

echo
echo "==> Building a one-file bundle (for manual distribution)"
flatpak build-bundle \
  "${REPO_DIR}" \
  "${ROOT}/build/${APP_ID}-${RUNTIME_VERSION}.flatpak" \
  "${APP_ID}" \
  "${RUNTIME_VERSION}"

echo
echo "==> Done. Install with:"
echo "    flatpak install --user $(pwd)/../${APP_ID}-${RUNTIME_VERSION}.flatpak"
echo "    flatpak run ${APP_ID}"
