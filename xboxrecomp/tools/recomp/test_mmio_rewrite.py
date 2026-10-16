"""Tests for the MMIO accessor rewrite (tools/recomp/mmio_rewrite.py)."""

from tools.recomp.mmio_rewrite import rewrite


def test_read_becomes_mmio_read():
    assert rewrite("eax = MEM32(esi + 0x10);") == "eax = MMIO_RD32(esi + 0x10);"


def test_signed_read_keeps_its_sign():
    assert rewrite("eax = (uint32_t)SMEM16(ecx);") == \
        "eax = (uint32_t)((int16_t)MMIO_RD16(ecx));"


def test_plain_store():
    assert rewrite("    MEM8(eax + -20971253) = 2;") == \
        "    MMIO_WR8(eax + -20971253, (2));"


def test_compound_store_reads_then_writes():
    assert rewrite("MEM32(ebp + -4) &= 0;") == \
        "MMIO_WR32(ebp + -4, MMIO_RD32(ebp + -4) & (0));"


def test_nested_access_in_address_and_value():
    got = rewrite("MEM32(MEM32(esi) * 4 + 0x335DE0) = MEM16(edx) + 1;")
    assert got == ("MMIO_WR32(MMIO_RD32(esi) * 4 + 0x335DE0, "
                   "(MMIO_RD16(edx) + 1));")


def test_comparison_is_not_a_store():
    assert rewrite("if (MEM32(eax) == 0) goto x;") == \
        "if (MMIO_RD32(eax) == 0) goto x;"


def test_other_accessors_untouched():
    # XMM_MEM is not rewritten; MEMF/SMEM64 get their tiled-aperture folding
    # forms (test_block_forms_and_wide_accessors_fold_the_tiled_aperture).
    src = "xmm0 = XMM_MEM(ebp + 8);"
    assert rewrite(src) == src
    assert rewrite("f = MEMF(eax); SMEM64(eax);") == \
        "f = DEV_MEMF(eax); DEV_SMEM64(eax);"


def test_shift_assign():
    assert rewrite("MEM32(eax) <<= ecx;") == \
        "MMIO_WR32(eax, MMIO_RD32(eax) << (ecx));"


def test_signed_store():
    assert rewrite("SMEM32(ebp + -8) = (int32_t)x; fp_pop();") == \
        "MMIO_WR32(ebp + -8, ((int32_t)x)); fp_pop();"


def test_signed_compound_store():
    assert rewrite("SMEM16(eax) >>= 1;") == \
        "MMIO_WR16(eax, ((int16_t)MMIO_RD16(eax)) >> (1));"


def test_block_forms_and_wide_accessors_fold_the_tiled_aperture():
    # rep movsd's block copy and the float/double/64-bit accessors are not
    # MMIO_* accesses; they get the folding forms instead.
    got = rewrite("uint8_t *_d = (uint8_t*)XBOX_PTR(edi); f = MEMF(esi); d = MEMD(ebx); q = SMEM64(ecx);")
    assert got == ("uint8_t *_d = (uint8_t*)XBOX_DEV_PTR(edi); f = DEV_MEMF(esi);"
                   " d = DEV_MEMD(ebx); q = DEV_SMEM64(ecx);")
    # Idempotent: a second pass leaves the folded forms alone.
    assert rewrite(got) == got
