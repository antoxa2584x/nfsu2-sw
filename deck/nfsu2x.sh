#!/bin/sh
# Need for Speed: Underground 2 (Xbox recomp) -- Steam Deck launcher.
#
# Add this file to Steam (Desktop Mode: Steam -> Add a Game -> Add a
# Non-Steam Game -> Browse -> nfsu2x.sh) and start it from Game Mode.
#
# Runs from its own folder, so the options (nfsu2x_options.txt) and the
# shader/pipeline caches live next to it. The disc is game/ here unless
# NFSU2_GAME_DIR says otherwise. KEY=VALUE lines in nfsu2x_env.txt are
# exported first; a variable already set (Steam launch options
# "KEY=VALUE %command%") wins over the file.
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE" || exit 1

if [ -f nfsu2x_env.txt ]; then
    while IFS= read -r line || [ -n "$line" ]; do
        line="$(printf '%s' "$line" | tr -d '\r')"
        case "$line" in
            ''|\#*) continue ;;
            *=*) ;;
            *) continue ;;
        esac
        key="${line%%=*}"
        case "$key" in
            *[!A-Za-z0-9_]*|'') continue ;;
        esac
        eval "isset=\${$key+x}"
        [ -z "$isset" ] && export "$line"
    done < nfsu2x_env.txt
fi
export NFSU2_GAME_DIR="${NFSU2_GAME_DIR:-$HERE/game}"

# The window's class (taskbar icon via the .desktop entry's StartupWMClass)
# and its own icon.
export SDL_VIDEO_X11_WMCLASS="${SDL_VIDEO_X11_WMCLASS:-nfsu2x}"
export SDL_VIDEO_WAYLAND_WMCLASS="${SDL_VIDEO_WAYLAND_WMCLASS:-nfsu2x}"
export RECOMP_WINDOW_ICON="${RECOMP_WINDOW_ICON:-$HERE/nfsu2x.bmp}"

# Menu/desktop entry with the icon (nfsu2x-install.sh), written or fixed
# whenever it is missing or points at another folder.
ENTRY="${XDG_DATA_HOME:-$HOME/.local/share}/applications/nfsu2x.desktop"
if [ -f "$HERE/nfsu2x-install.sh" ] && ! grep -qF "Path=$HERE" "$ENTRY" 2>/dev/null; then
    sh "$HERE/nfsu2x-install.sh" --quiet
fi

[ -f nfsu2x_log.txt ] && mv -f nfsu2x_log.txt nfsu2x_log.prev.txt
exec "$HERE/nfsu2_recomp" "$@" >nfsu2x_log.txt 2>&1
