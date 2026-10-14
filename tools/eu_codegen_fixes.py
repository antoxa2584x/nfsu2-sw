"""Bounded postprocessing for verified PAL instructions the lifter cannot emit."""
import hashlib
import re
import struct

AUDIO_SPAN_SHA = '715b3b7782de2313fabcdb133943f2aa7ef37e449f76e9369c9b46b4634b3d53'
PUSH = 'RECOMP_UNIMPL("pushfd", 0x0027E917u); /* TODO: pushfd */'
POP = 'RECOMP_UNIMPL("popfd", 0x0027E921u); /* TODO: popfd */'
SAVE = '''/* The only reaching path follows CMP EAX,0xEE with equality.
     * Keep the modeled DF/IF and the original four-byte stack slot. */
    _eu_audio_saved_flags = 0x46u | (g_df ? 0x400u : 0u)
                         | (nfsu2_eu_interrupt_model_if() ? 0x200u : 0u);
    PUSH32(esp, _eu_audio_saved_flags);'''
RESTORE = '''{ uint32_t _eu_audio_restored_flags;
      POP32(esp, _eu_audio_restored_flags);
      if (_eu_audio_restored_flags != _eu_audio_saved_flags)
          RECOMP_UNIMPL("modified audio marker saved flags", 0x0027E921u);
      g_df = (_eu_audio_restored_flags >> 10) & 1u;
      _fa = _fb = 0; _fas = _fbs = 0; /* equality; next loop CMP overwrites */
    }'''


def xbe_span(data, address, size):
    base = struct.unpack_from('<I', data, 0x104)[0]
    count, table = struct.unpack_from('<II', data, 0x11C)
    for i in range(count):
        _, va, _, raw, raw_size = struct.unpack_from('<5I', data, table-base+56*i)
        if va <= address < address+size <= va+raw_size:
            result = data[raw+address-va:raw+address-va+size]
            if len(result) == size:
                return result
    raise ValueError('Verified audio span is outside the XBE sections')


def function(text, address):
    match = re.search(r'^void sub_'+address+r'\(void\)\s*\{\n.*?^\}', text, re.M|re.S)
    if not match:
        raise ValueError('Missing verified audio function '+address)
    return match.group()


def fix_audio_flags(output, data):
    # The hash covers the entire caller and its sole direct callee. The callee
    # does not observe the saved flags slot or modify modeled control flags.
    if hashlib.sha256(xbe_span(data, 0x27E864, 0x172)).hexdigest() != AUDIO_SPAN_SHA:
        raise ValueError('Verified PAL audio marker bytes differ')
    candidates = [p for p in output.glob('recomp_*.c')
                  if 'void sub_0027E8E1(void)' in p.read_text(encoding='utf-8')]
    if len(candidates) != 1:
        raise ValueError('Expected exactly one PAL audio marker caller')
    path = candidates[0]
    text = path.read_text(encoding='utf-8')
    caller = function(text, '0027E8E1')
    callee = function(text, '0027E864')
    if any(token in callee for token in
           ('RECOMP_UNIMPL', 'RECOMP_ABI_CALL', 'RECOMP_ICALL', 'RECOMP_ITAIL')):
        raise ValueError('Verified audio callee shape changed')
    if callee.count('MEM32(ebp + 8)') != 1 or 'ebp + 0xC' in callee:
        raise ValueError('Verified audio callee stack access changed')
    if SAVE in caller and RESTORE in caller and PUSH not in caller and POP not in caller:
        return path  # Repeated validation leaves already-correct output untouched.
    if caller.count(PUSH) != 1 or caller.count(POP) != 1:
        raise ValueError('Verified audio PUSHFD/POPFD pair changed')
    if caller.count('    uint32_t ebp = 0;') != 1 or '_eu_audio_saved_flags' in caller:
        raise ValueError('Verified audio local state changed')
    if 'if (CMP_NE(_fa, _fb)) goto loc_0027E939;' not in caller or \
       'PUSH32(esp, 0x0027E91Eu); RECOMP_ABI_CALL(0x0027E864u, sub_0027E864);' not in caller:
        raise ValueError('Verified audio equality path changed')
    patched = caller.replace('    uint32_t ebp = 0;',
                            '    uint32_t _eu_audio_saved_flags = 0;\n    uint32_t ebp = 0;', 1)
    patched = patched.replace(PUSH, SAVE).replace(POP, RESTORE)
    text = text.replace(caller, patched, 1)
    if '#include "eu_interrupt.h"' not in text:
        text = text.replace('#include "recomp_funcs.h"',
                            '#include "recomp_funcs.h"\n#include "eu_interrupt.h"', 1)
    path.write_text(text, encoding='utf-8', newline='\n')
    return path
