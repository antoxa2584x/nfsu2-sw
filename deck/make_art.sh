#!/usr/bin/env bash
# Steam library artwork for the non-Steam shortcut (nfsu2x-steam-art.py
# copies it into Steam's grid folder under the shortcut's app id):
#
#   capsule.png  600x900   vertical (library grid)   assets/steam_capsule.png -> <appid>p.png
#   wide.png     920x430   horizontal                assets/steam_wide.png    -> <appid>.png
#   hero.png     1920x620  game page banner (Steam draws the logo over it)
#                                            assets/steam_hero.png    -> <appid>_hero.png
#   logo.png     assets/nfsu2_logo.png                               -> <appid>_logo.png
#   icon.png     assets/icon.jpg                                     -> <appid>_icon.png
#
# capsule/wide/hero/logo are from SteamGridDB.
#
#   deck/make_art.sh <out dir>      (ImageMagick 6 `convert`)
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:?usage: make_art.sh <out dir>}"
A="$REPO/assets"
mkdir -p "$OUT"

convert "$A/steam_capsule.png" -strip "$OUT/capsule.png"
convert "$A/steam_wide.png" -strip "$OUT/wide.png"
convert "$A/steam_hero.png" -strip "$OUT/hero.png"
convert "$A/nfsu2_logo.png" -strip "$OUT/logo.png"
convert "$A/icon.jpg" -strip "$OUT/icon.png"
