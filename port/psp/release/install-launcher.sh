#!/bin/sh
# Adds MvC2 EBOOT Builder, with its icon, to this user's applications menu.
# Run it again if you move this folder.
set -e
here=$(cd "$(dirname "$0")" && pwd)
apps=${XDG_DATA_HOME:-$HOME/.local/share}/applications
mkdir -p "$apps"
cat > "$apps/mvc2-eboot-builder.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=MvC2 EBOOT Builder
Comment=Build the MvC2 PSP EBOOT from your own PS2 disc image
Exec="$here/mvc2-eboot-builder" %f
Icon=$here/mvc2-eboot-builder.png
Terminal=true
Categories=Game;Utility;
EOF
chmod +x "$here/mvc2-eboot-builder"
echo "Added to your applications menu: $apps/mvc2-eboot-builder.desktop"
