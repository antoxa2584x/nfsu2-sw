"""Route a lifted function's memory accesses through MMIO-aware accessors.

A title's statically linked XDK libraries drive hardware registers directly --
DirectSound programs the APU at 0xFE800000 with ordinary `mov`s. The lifted
code turns those into MEM32(addr) like any other access, which only works
against a register block if the host traps the access (a page fault and an
instruction decoder, per host architecture). That exists on Windows/x86-64
alone.

For functions known to talk to hardware, this rewrites the generated C so
every access asks the runtime instead:

    MEM32(a)             -> MMIO_RD32(a)
    SMEM16(a)            -> ((int16_t)MMIO_RD16(a))
    MEM32(a) = v;        -> MMIO_WR32(a, (v));
    MEM32(a) |= v;       -> MMIO_WR32(a, MMIO_RD32(a) | (v));

MMIO_RDn/MMIO_WRn (recomp_types.h) take the plain-memory path unless the
address is in the device range, so the cost is one compare per access, paid
only in the functions selected -- the rest of the title is untouched.

The rewrite works on the C text: the lifter builds memory operands in many
places, and a textual pass over its output is the one point they all meet.
"""

import re

_ACCESS = re.compile(r"(?<![A-Za-z0-9_])(S?)MEM(8|16|32)\(")
_ASSIGN = re.compile(r"\s*(<<=|>>=|\+=|-=|\*=|/=|%=|&=|\|=|\^=|=(?!=))")
_SIGNED = {"8": "int8_t", "16": "int16_t", "32": "int32_t"}


def _close_paren(s, i):
    """Index just past the ')' matching the '(' at s[i-1]."""
    depth = 1
    while i < len(s):
        c = s[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError("unbalanced parentheses in generated code")


def _statement_end(s, i):
    """Index of the ';' ending the expression that starts at i (depth 0)."""
    depth = 0
    while i < len(s):
        c = s[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == ";" and depth == 0:
            return i
        i += 1
    raise ValueError("unterminated statement in generated code")


# Accesses the MMIO_* accessors do not cover still see the tiled framebuffer
# aperture (0xF0000000, a view of the contiguous window). Where the host
# cannot alias memory (Horizon) nothing is mapped there, so these go through
# XBOX_DEV_FOLD (recomp_types.h) as well: XBOX_PTR in the block-copy and
# block-fill forms of rep movs/stos, and the float/double/64-bit accessors.
# NFS Carbon's D3DX copies surfaces into it with rep movsd.
_DEV_NAMES = re.compile(r"(?<![A-Za-z0-9_])(XBOX_PTR|MEMF|MEMD|SMEM64)\(")


def rewrite(code):
    """Rewrite every memory access in `code` to its device-aware form."""
    return _DEV_NAMES.sub(lambda m: {"XBOX_PTR": "XBOX_DEV_PTR("}.get(
        m.group(1), "DEV_" + m.group(1) + "("), _rewrite_mem(code))


def _rewrite_mem(code):
    """Rewrite every MEM8/16/32 and SMEM8/16/32 access in `code`."""
    out = []
    pos = 0
    while True:
        m = _ACCESS.search(code, pos)
        if not m:
            out.append(code[pos:])
            return "".join(out)
        out.append(code[pos:m.start()])
        signed, bits = m.group(1), m.group(2)
        args_end = _close_paren(code, m.end())
        addr = _rewrite_mem(code[m.end():args_end - 1])
        a = _ASSIGN.match(code, args_end)
        if a:
            # A signed accessor as an lvalue (fistp stores through SMEM32)
            # writes the same bits; only a compound op needs the signed read.
            op = a.group(1)
            end = _statement_end(code, a.end())
            value = _rewrite_mem(code[a.end():end].strip())
            cur = f"MMIO_RD{bits}({addr})"
            if signed:
                cur = f"(({_SIGNED[bits]}){cur})"
            if op == "=":
                out.append(f"MMIO_WR{bits}({addr}, ({value}))")
            else:
                out.append(f"MMIO_WR{bits}({addr}, {cur} {op[:-1]} ({value}))")
            pos = end
            continue
        read = f"MMIO_RD{bits}({addr})"
        if signed:
            read = f"(({_SIGNED[bits]}){read})"
        out.append(read)
        pos = args_end
