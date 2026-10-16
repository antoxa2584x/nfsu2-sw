#!/usr/bin/env python3
"""Prepare verified European native overrides and movie/matrix fast paths.

Private output belongs outside the source tree. The source NTSC overrides remain
unchanged; unverified title-specific fast paths are not transplanted.
"""
import argparse
import hashlib
from pathlib import Path

PAL_SHA1 = '144deb954e0e9208e0b7e08e09b4e5e67b2f3356'


def prepare(xbe, output):
    if hashlib.sha1(xbe.read_bytes()).hexdigest() != PAL_SHA1:
        raise ValueError('This profile requires the verified Europe En/Fr/De/It/Nl/Sv executable')
    root = Path(__file__).resolve().parent.parent
    source = (root / 'src/recomp_manual.c').read_text()
    # Verified against the European disassembly: both CRT copies have the same
    # 674-byte body; XDK graphics/audio preserve the fields used by these hooks.
    pieces = [source[:source.index('/* ── Bounding box')],
              'void sub_002A9450(void) { crt_memmove(); }\n',
              source[source.index('/* ── D3D fence wait'):source.index('/* ── VP6 movie frames')],
              source[source.index('/* ── VP6 movie frames'):source.index('/* ── Float to integer')],
              source[source.index('/* ── Movie colour conversion'):source.index('/* ── Options -> Video')],
              # The added Options rows (Video, Vibration, kept Audio volumes)
              # hook NTSC-U menu code and data that have no PAL mapping yet.
              'void nfsu2_options_load(void) { }\n',
              source[source.index('/* ── Manual function overrides'):]]
    adapted = '\n'.join(pieces)

    addresses = {
        0x2A7EE0: 0x2A8470, 0x2A9450: 0x2A99E0,
        0x2E8F20: 0x2E9540, 0x2F1D80: 0x2F23A0, 0x2F22F0: 0x2F2910,
        0x2F7798: 0x2F7DB8, 0x2E953C: 0x2E9B5C,
        0x32EB65: 0x32F185, 0x33518D: 0x3357AD,
        0x3351A1: 0x3357C1, 0x32BFF0: 0x32C610,
        0x2618F0: 0x261E90, 0x469D04: 0x46A444,
        0x25ECB4: 0x25F254, 0x3D0810: 0x3D0F50,
        0x3D1010: 0x3D1750, 0x3D1810: 0x3D1F50,
        0x129610: 0x129A50, 0x2213EA: 0x22199A,
        0x335DE0: 0x336400, 0x3351F4: 0x335814,
        0x334F97: 0x3355B7, 0x3351FC: 0x33581C, 0x32C012: 0x32C632,
    }
    import re
    # One simultaneous replacement avoids remapping already-adapted addresses.
    def substitute(match):
        token = match.group(0)
        n = int(token[4:] if token.startswith('sub_') else token[2:], 16)
        if n not in addresses:
            return token
        return ('sub_%08X' if token.startswith('sub_') else '0x%08X') % addresses[n]
    adapted = re.sub(r'sub_[0-9A-Fa-f]{8}|0x[0-9A-Fa-f]{6,8}', substitute, adapted)
    # Optional frame-lag detection embeds a title-specific return PC. Keep it
    # off for PAL until that call chain has been validated in a race.
    adapted = adapted.replace('if (MEM32(esp) == 0x002E9B5Cu && MEM32(esp + 12) == 0x000AEDDBu)', 'if (0)')
    # PAL resource lookup (00128E40) walks intrusive nodes at +4, with
    # metadata at +10 and file data at +0C. Parsed package records (30210)
    # store the uppercase name hash at +8; raw FEng packages store it as a
    # string at +28. This includes pause/options during free roam as well.
    helper = """
#if defined(__APPLE__)
static int pal_guest_range(uint32_t va, uint32_t size)
{
    uint32_t offset = va & 0x7FFFFFFFu;
    return offset >= 0x10000u && offset < 0x04000000u
        && size <= 0x04000000u - offset;
}
static uint32_t pal_name_hash(uint32_t va)
{
    uint32_t h = 0xFFFFFFFFu;
    for (unsigned i = 0; i < 128 && pal_guest_range(va + i, 1); i++) {
        unsigned c = MEM8(va + i);
        if (!c) return h;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        h = h * 33u + c;
    }
    return 0;
}
static int pal_pause_package(void)
{
    uint32_t link = MEM32(0x004073A4u);
    for (unsigned i = 0; i < 128 && link >= 4; i++) {
        uint32_t node = link - 4;
        if (node == 0x004073A0u || !pal_guest_range(node, 0x14)) break;
        uint32_t info = MEM32(node + 0x10), hash = 0;
        if (pal_guest_range(info, 12)) {
            uint32_t kind = MEM32(info);
            if (kind == 0x30210u) hash = MEM32(info + 8);
            else if (kind == 0x30203u) {
                uint32_t data = MEM32(node + 0xC);
                if (pal_guest_range(data, 0x29) && MEM32(data) == 0xE76E4546u
                    && MEM32(data + 8) == 0x64486B50u && MEM32(data + 0x10) >= 0x20000u)
                    hash = pal_name_hash(data + 0x28);
            }
        }
        if (hash == PAUSE_HASH || hash == OPTIONS_HASH || hash == OPTIONS_MAIN_HASH) return 1;
        uint32_t next = MEM32(node + 4);
        if (next == link) break;
        link = next;
    }
    return 0;
}
static void pal_publish_ui(void)
{
    static int registered;
    if (!registered) {
        registered = 1;
        nfsu2_mobile_register_poll(pal_publish_ui);
    }
    uint32_t flow = MEM32(0x00429574u), race = MEM32(0x0042AE34u);
    nfsu2_mobile_ui_state(flow, race, MEM32(0x0042AE54u),
                         flow == 6 && race != 6 ? pal_pause_package() : 0);
}
#endif
"""
    def name_hash(name):
        value = 0xFFFFFFFF
        for char in name.upper(): value = (value * 33 + ord(char)) & 0xFFFFFFFF
        return '0x%08Xu' % value
    for token, name in [('PAUSE_HASH', 'UI_Pause.fng'), ('OPTIONS_HASH', 'UI_PauseOptions.fng'),
                        ('OPTIONS_MAIN_HASH', 'UI_PauseOptionsMain.fng')]:
        helper = helper.replace(token, name_hash(name))
    adapted = adapted.replace('/* ── D3D fence wait', helper + '\n/* ── D3D fence wait')
    # Sample actual display activity rather than BlockOnTime: uncapped
    # racing can avoid fence waits entirely. Both paths hold the guest lock;
    # UIKit consumes only the atomic snapshot, never raw guest memory.
    adapted = adapted.replace('    xbox_Nv2aVblankTaken();',
        '#if defined(__APPLE__)\n    pal_publish_ui();\n#endif\n    xbox_Nv2aVblankTaken();')
    # The verified XDK software-flip encoding keeps buffers/fences intact.
    # Only its vblank interval is changed, under the existing trap lock.
    adapted = adapted.replace('    sub_002F2910_gen();',
        '#if defined(__APPLE__)\n    uint32_t saved_flip = nv[0x400708 / 4];\n    if (nsource && (nv[0x400704 / 4] & 0x1FFCu) == 0x100u) {\n        if ((saved_flip & 31u) == 1u) pal_publish_ui();\n        nv[0x400708 / 4] = xbox_apple_immediate_flip(saved_flip);\n    }\n#endif\n    sub_002F2910_gen();\n#if defined(__APPLE__)\n    nv[0x400708 / 4] = saved_flip;\n#endif')
    adapted = '#if defined(__APPLE__)\n#include "apple/apple_game_state.h"\n#include "platform/apple_present.h"\n#endif\n' + adapted
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('/* European profile; produced by tools/prepare_pal.py. */\n' + adapted)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('xbe', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    prepare(a.xbe, a.output)
