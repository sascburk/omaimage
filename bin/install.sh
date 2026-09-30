#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
qmake6 omaimage.pro
make -j"$(nproc)"
mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications" "$HOME/.local/share/icons/hicolor/scalable/apps"
install -Dm755 "$root/omaimage" "$HOME/.local/bin/omaimage"
sed "s|^Exec=.*|Exec=$HOME/.local/bin/omaimage|" "$root/packaging/omaimage.desktop" \
  > "$HOME/.local/share/applications/omaimage.desktop"
install -Dm644 "$root/packaging/omaimage.svg" "$HOME/.local/share/icons/hicolor/scalable/apps/omaimage.svg"
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "$HOME/.local/share/applications" >/dev/null 2>&1 || true
fi
echo "Omaimage liegt unter $HOME/.local/bin/omaimage"
