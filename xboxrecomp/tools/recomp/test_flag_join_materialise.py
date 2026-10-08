"""A jcc after a join whose predecessors set the flags differently.

NFS Carbon's sub_002A87D0 reaches `jne` at 0x2A8813 from `test al, al`
(by jmp) and from `inc al` (by fall-through). The states do not merge, and the
branch lifted to the `_flags` fallback -- never taken -- so the function always
said a car may not smack a prop. Each predecessor now computes the condition
into `_mfN` on its way out.
"""
from tools.recomp import config
from tools.recomp.translator import FunctionTranslator

BASE = 0x10000


def translate(image):
    config._install([config.Section('.text', BASE, len(image), 0, len(image), True)],
                    entry_point=BASE, kernel_thunk_addr=BASE,
                    origin='flag-join-materialise-test')
    db = {BASE: {'start': hex(BASE), 'end': BASE + len(image),
                 '_addr': BASE, 'size': len(image)}}
    return FunctionTranslator(image, db).translate_function(BASE, db[BASE])


def test_inc_and_test_join_for_jne():
    # 0: test al,al; je 8; 4: inc al; jmp 10; 8: test al,al;
    # 10: jne 15; xor eax,eax; ret; 15: mov al,1; ret
    code = translate(bytes.fromhex('84c07404fec0eb0284c0750331c0c3b001c3'))
    assert 'if (_flags /* jne' not in code, code
    assert '_mf0' in code, code
    assert code.count('/* flags for loc_') == 2, code


def test_zf_only_merge_still_materialises_jns():
    # 0: test al,al; je 9; 4: sub edx,0x10; jmp 12; 9: sub edx,8;
    # 12: jns 17; xor eax,eax; ret; 17: mov al,1; ret
    # Both predecessors write edx, so the merge keeps "ZF from edx" -- which
    # answers je/jne, not jns. NFS Carbon's HUFF bit reader (sub_001DCFA0,
    # 0x1DD164) lifted that jns as never taken.
    code = translate(bytes.fromhex('84c0740583ea10eb0383ea08790331c0c3b001c3'))
    assert 'if (_flags /* jns' not in code, code
    assert '_mf0' in code, code
    assert code.count('/* flags for loc_') == 2, code
