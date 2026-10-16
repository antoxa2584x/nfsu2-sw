"""A call target that splits a decoded instruction is dropped only when every
call to it comes from outside the detected functions (a call decoded out of
data, like NFS Carbon's XPP header calling into its USB enumerator)."""
from types import SimpleNamespace as NS

from tools.disasm.functions import FunctionDetector


def detector(callers):
    det = FunctionDetector.__new__(FunctionDetector)
    det._split_call_targets = {0x1050}
    det._candidates = {0x1000: (0.9, "prologue"), 0x1050: (0.8, "call_target")}
    det.functions = {0x1000: NS(start=0x1000, end=0x1100)}
    det.engine = NS(instructions={a: NS(address=a, is_call=True, call_target=0x1050)
                                  for a in callers})
    return det


def test_split_target_called_only_from_data_is_dropped():
    det = detector([0x2000])            # caller outside every function
    assert det._prune_data_call_targets()
    assert 0x1050 not in det._candidates


def test_split_target_called_from_code_is_kept():
    det = detector([0x2000, 0x1010])    # one caller inside sub_1000
    assert not det._prune_data_call_targets()
    assert 0x1050 in det._candidates
