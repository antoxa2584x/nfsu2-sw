#!/usr/bin/env bash
# Translate the verified European disc. Keep private output outside the repo.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${NFSU2_XBE:?Set NFSU2_XBE to your European default.xbe}"
: "${NFSU2_GEN_DIR:?Set NFSU2_GEN_DIR to a private output directory}"
PY="${PYTHON:-python3}"
WORK="${NFSU2_PAL_WORK:-$(dirname "$NFSU2_GEN_DIR")}"
mkdir -p "$WORK" "$NFSU2_GEN_DIR"
"$PY" "$HERE/tools/prepare_pal.py" "$NFSU2_XBE" "$WORK/recomp_manual.c"
cd "$HERE/xboxrecomp"
if [ "${LIFT_ONLY:-0}" != "1" ]; then
    "$PY" -m tools.xbe_parser "$NFSU2_XBE" --json "$WORK/pal-analysis.json" --quiet
    "$PY" -m tools.disasm "$NFSU2_XBE" --analysis-json "$WORK/pal-analysis.json" --seed-functions "$HERE/config/seed_functions_pal.json" --output "$WORK/disasm"
    "$PY" -m tools.func_id "$NFSU2_XBE" --functions "$WORK/disasm/functions.json" \
        --strings "$WORK/disasm/strings.json" --xrefs "$WORK/disasm/xrefs.json" --output "$WORK/func_id"
    "$PY" -m tools.abi_analysis "$NFSU2_XBE" --disasm-dir "$WORK/disasm" \
        --func-id-dir "$WORK/func_id" --output-dir "$WORK/abi"
fi
"$PY" -c 'from pathlib import Path; import sys; Path(sys.argv[1]).unlink(missing_ok=True)' "$NFSU2_GEN_DIR/recomp_types.h"
"$PY" -m tools.recomp "$NFSU2_XBE" --game-name "NFS Underground 2 PAL Europe" --all --split 250 \
    --gen-dir "$NFSU2_GEN_DIR" --disasm-dir "$WORK/disasm" --func-id-dir "$WORK/func_id" \
    --abi-dir "$WORK/abi" --exclude-manual "$WORK/recomp_manual.c" --mmio-sections DSOUND,XPP
printf 'Configure with -DNFSU2_REGION=PAL -DNFSU2_GEN_DIR=%s -DNFSU2_MANUAL_SOURCE=%s/recomp_manual.c\n' "$NFSU2_GEN_DIR" "$WORK"
