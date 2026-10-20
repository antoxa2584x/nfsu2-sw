#!/usr/bin/env python3
"""Steam library artwork for the non-Steam shortcut of this folder.

Copies art/ (capsule, wide, hero, logo, icon) into
<Steam>/userdata/<user>/config/grid/ under the shortcut's app id:
<id>p.png (vertical), <id>.png (horizontal), <id>_hero.png,
<id>_logo.png, <id>_icon.png. Steam shows them after its next start.

The app id comes from
  - SteamGameId, which Steam sets when it starts a non-Steam game
    ((appid << 32) | 0x02000000), and
  - every shortcut in shortcuts.vdf whose target is in this folder or whose
    name is the game's: its "appid" field, or for old clients
    crc32(exe + name) | 0x80000000.
Files already there are kept: they may be art picked in Steam. --force
replaces them, --quiet prints nothing. nfsu2x.sh runs this with --quiet on
every start from Steam; nfsu2x-install.sh runs it too.
"""
import os
import shutil
import sys
import zlib

NAME = "Need for Speed Underground 2"
HERE = os.path.dirname(os.path.realpath(__file__))
ART = os.path.join(HERE, "art")
FILES = (("capsule.png", "p.png"), ("wide.png", ".png"), ("hero.png", "_hero.png"),
         ("logo.png", "_logo.png"), ("icon.png", "_icon.png"))
QUIET = "--quiet" in sys.argv
FORCE = "--force" in sys.argv


def say(*a):
    if not QUIET:
        print(*a)


def cstr(buf, i):
    j = buf.index(b"\0", i)
    return buf[i:j].decode("utf-8", "replace"), j + 1


def parse(buf, i):
    """Steam's binary KeyValues: a map until 0x08."""
    d = {}
    while True:
        t = buf[i]
        i += 1
        if t == 0x08:
            return d, i
        key, i = cstr(buf, i)
        if t == 0x00:
            d[key], i = parse(buf, i)
        elif t == 0x01:
            d[key], i = cstr(buf, i)
        elif t == 0x02:
            d[key] = int.from_bytes(buf[i:i + 4], "little")
            i += 4
        elif t == 0x07:
            d[key] = int.from_bytes(buf[i:i + 8], "little")
            i += 8
        else:
            raise ValueError("unknown KeyValues type %d" % t)


def get(entry, key):
    for k, v in entry.items():
        if k.lower() == key.lower():
            return v
    return None


def ours(exe):
    path = exe.strip().strip('"')
    try:
        return os.path.realpath(path).startswith(HERE + os.sep)
    except OSError:
        return False


def shortcut_ids(config):
    """App ids of this game's shortcuts in one user's shortcuts.vdf."""
    ids = set()
    try:
        with open(os.path.join(config, "shortcuts.vdf"), "rb") as f:
            root, _ = parse(f.read(), 0)
    except (OSError, ValueError, IndexError):
        return ids
    for entry in (get(root, "shortcuts") or {}).values():
        if not isinstance(entry, dict):
            continue
        exe = get(entry, "exe") or ""
        name = get(entry, "appname") or ""
        if not (ours(exe) or name == NAME):
            continue
        appid = get(entry, "appid")
        if not appid:
            appid = zlib.crc32((exe + name).encode("utf-8")) | 0x80000000
        ids.add(appid & 0xFFFFFFFF)
    return ids


def steam_roots():
    seen, roots = set(), []
    home = os.path.expanduser("~")
    for r in (os.path.join(home, ".local/share/Steam"), os.path.join(home, ".steam/steam"),
              os.path.join(home, ".var/app/com.valvesoftware.Steam/data/Steam")):
        real = os.path.realpath(r)
        if os.path.isdir(os.path.join(real, "userdata")) and real not in seen:
            seen.add(real)
            roots.append(real)
    return roots


def main():
    env_id = None
    try:
        gid = int(os.environ.get("SteamGameId", "0"))
        if gid >> 32:
            env_id = gid >> 32
    except ValueError:
        pass
    if not os.path.isdir(ART):
        say("no art/ next to this script")
        return
    done = 0
    for root in steam_roots():
        udir = os.path.join(root, "userdata")
        for user in os.listdir(udir):
            config = os.path.join(udir, user, "config")
            if not user.isdigit() or not os.path.isdir(config):
                continue
            ids = shortcut_ids(config)
            if env_id and (not ids or env_id not in ids) and \
                    os.path.exists(os.path.join(config, "shortcuts.vdf")):
                ids.add(env_id)
            if not ids:
                continue
            grid = os.path.join(config, "grid")
            os.makedirs(grid, exist_ok=True)
            for appid in sorted(ids):
                for src, suffix in FILES:
                    s = os.path.join(ART, src)
                    d = os.path.join(grid, "%d%s" % (appid, suffix))
                    if os.path.exists(s) and (FORCE or not os.path.exists(d)):
                        shutil.copyfile(s, d)
                        done += 1
                say("Steam user %s: artwork for shortcut %d in %s" % (user, appid, grid))
    if done:
        say("%d file(s) added; restart Steam to see them" % done)
    else:
        say("nothing added (no shortcut of this game found, or its art is already there)")


if __name__ == "__main__":
    try:
        main()
    except Exception as e:      # never stop the game from starting
        if not QUIET:
            print("artwork: %s" % e, file=sys.stderr)
