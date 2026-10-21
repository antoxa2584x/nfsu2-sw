"""Check displayed FPS timing and overlay bounds without GPU or game assets."""
from pathlib import Path
import argparse, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[2]
def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', default='clang')
    args = ap.parse_args()
    code = r'''
#include <assert.h>
#include "fps_overlay.h"
int main(void) {
    FpsCounter c = {0};
    fps_counter_present(&c, 0);
    assert(!c.valid && !c.frames);
    for (unsigned i=1; i<=30; ++i) fps_counter_present(&c, (uint64_t)i*500000000/30);
    assert(c.valid && c.tenths==600);
    for (unsigned i=1; i<=10; ++i) fps_counter_present(&c, 500000000ull+(uint64_t)i*50000000);
    assert(c.tenths==200);
    fps_counter_present(&c, 3000000000ull);
    assert(c.tenths==5);
    fps_counter_present(&c, 1);
    assert(!c.frames && c.window_ns==1);
    struct { uint32_t before, pixels[FPS_OVERLAY_W*FPS_OVERLAY_H], after; } guarded;
    guarded.before=0xdeadbeef; guarded.after=0xcafebabe;
    fps_overlay_pixels(guarded.pixels, 9999, 1);
    assert(guarded.before==0xdeadbeef && guarded.after==0xcafebabe);
    unsigned white=0;
    for (unsigned i=0;i<FPS_OVERLAY_W*FPS_OVERLAY_H;i++) {
        assert(guarded.pixels[i]==0xff151515 || guarded.pixels[i]==0xffffffff);
        white += guarded.pixels[i]==0xffffffff;
    }
    assert(white);
    fps_overlay_pixels(guarded.pixels, 0, 0);
    assert(guarded.before==0xdeadbeef && guarded.after==0xcafebabe);
    puts("PASS: 60/20/0.5 FPS, clock reset and maximum-label pixel bounds");
}
'''
    build=ROOT/'build-eu';build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='fps-overlay-', dir=build) as directory:
        work=Path(directory);source=work/'test.c';exe=work/'test.exe'
        source.write_text('#include <stdio.h>\n'+code, encoding='utf-8', newline='\n')
        subprocess.run([args.cc,'-O2','-I'+str(ROOT/'xboxrecomp/src/nv2a_vk'),str(source),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
if __name__=='__main__': main()
