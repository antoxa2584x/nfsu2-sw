#!/bin/sh
# Registers the game with the desktop (and so with Steam): writes
# nfsu2x.desktop pointing at this folder, with its name and icon, into
# ~/.local/share/applications (the menu, and Steam's Add a Non-Steam Game
# list) and onto the desktop. Then in Desktop Mode: Steam -> Add a
# Non-Steam Game -> tick "Need for Speed Underground 2" -> Add Selected
# Programs. Steam takes the name and icon from that entry (a bare
# nfsu2x.sh has none).
#
# nfsu2x.sh runs this by itself (--quiet) whenever the entry is missing or
# points at another folder, so moving the folder needs nothing extra.
QUIET=0
[ "${1:-}" = "--quiet" ] && QUIET=1
say() { [ "$QUIET" = 1 ] || echo "$@"; }

HERE="$(cd "$(dirname "$0")" && pwd)"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
DESK="$(xdg-user-dir DESKTOP 2>/dev/null)"
[ -n "$DESK" ] || DESK="$HOME/Desktop"

chmod +x "$HERE/nfsu2x.sh" "$HERE/nfsu2_recomp" "$HERE/nfsu2x-install.sh" 2>/dev/null
mkdir -p "$APPS" || exit 1

# StartupWMClass matches the window class nfsu2x.sh gives the game, so the
# taskbar shows this icon on the running window too.
write_entry() {
    cat > "$1" <<EOF
[Desktop Entry]
Type=Application
Name=Need for Speed Underground 2
Comment=Xbox static recompilation
Exec="$HERE/nfsu2x.sh"
Path=$HERE
Icon=$HERE/nfsu2x.png
Terminal=false
Categories=Game;
StartupWMClass=nfsu2x
EOF
    chmod +x "$1"
}

write_entry "$APPS/nfsu2x.desktop"
say "Added $APPS/nfsu2x.desktop"
if [ -d "$DESK" ]; then
    write_entry "$DESK/nfsu2x.desktop"
    # GNOME-style desktops want the launcher marked trusted; KDE needs +x.
    command -v gio >/dev/null && gio set "$DESK/nfsu2x.desktop" metadata::trusted true 2>/dev/null
    say "Added $DESK/nfsu2x.desktop"
fi

# Refresh the menus: KDE reads its own cache (kbuildsycoca), others the
# desktop database.
for k in kbuildsycoca6 kbuildsycoca5; do
    if command -v "$k" >/dev/null; then
        "$k" >/dev/null 2>&1
        break
    fi
done
command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" 2>/dev/null
say "Now: Steam -> Add a Non-Steam Game -> Need for Speed Underground 2"
exit 0
