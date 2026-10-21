"""Exercise the actual EEPROM API and EU flip handlers under a competing retire."""
from pathlib import Path
import argparse
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def function(text, name):
    match = re.search(r'^(?:static )?[^\n]+\b' + re.escape(name) + r'\([^)]*\)\s*\{', text, re.M)
    if not match:
        raise ValueError('Missing function: ' + name)
    end = text.index('\n}\n', match.end())
    return text[match.start():end + 3]

def run(cc, folder, name, code, *args):
    src, exe = folder / (name + '.c'), folder / (name + '.exe')
    src.write_text(code, encoding='utf-8')
    subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe), *args], check=True)

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', default='clang')
    ap.add_argument('--gen-dir', type=Path)
    ap.add_argument('--xbe', type=Path)
    args = ap.parse_args()
    kernel = (ROOT / 'xboxrecomp/src/kernel/kernel_xbox.c').read_text()
    header = (ROOT / 'xboxrecomp/src/kernel/kernel.h').read_text()
    constants = '\n'.join(line for line in header.splitlines() if line.startswith('#define XC_'))
    api = 'static ULONG s_console_language = 1;\n' + function(kernel, 'xbox_SetConsoleLanguage')
    api += function(kernel, 'xbox_ExQueryNonVolatileSetting')
    prefix = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef __stdcall
#define __stdcall
#define xbox_log(...) ((void)0)
typedef uint32_t ULONG, *PULONG;
typedef int32_t LONG, *PLONG, NTSTATUS;
typedef void *PVOID;
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER ((int32_t)0xC000000D)
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);exit(2); } } while (0)
static int xbox_video_widescreen(void) { return 1; }
'''
    suffix = r'''
static void query(uint32_t expected) {
    ULONG value=99,type=99,size=99;
    CHECK(xbox_ExQueryNonVolatileSetting(XC_LANGUAGE,&type,&value,4,&size)==0);
    CHECK(value==expected && type==4 && size==4);
    value=99;
    CHECK(xbox_ExQueryNonVolatileSetting(XC_LANGUAGE,&type,&value,3,&size)==0 && value==99);
}
int main(void) {
    query(1);
    for (unsigned language=1;language<=9;++language) { xbox_SetConsoleLanguage(language);query(language); }
    xbox_SetConsoleLanguage(0);query(1);
    xbox_SetConsoleLanguage(100);query(1);
    CHECK(xbox_ExQueryNonVolatileSetting(XC_LANGUAGE,0,0,4,0)==STATUS_INVALID_PARAMETER);
    puts("PASS: original EEPROM API exposes host language, preserves fallback and bounds");
}
'''
    eu = (ROOT / 'src/regions/eu/recomp_manual.c').read_text()
    wrappers = '\n'.join(function(eu, name) for name in ['d3d_isr_enter', 'd3d_isr_leave', 'sub_002F23A0', 'sub_002F2910'])
    irq = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);exit(2); } } while (0)
static uint32_t ecx=0x1000, head, reported, retired, suspended, resumed, trap_locked, trap_taken;
static uint32_t nv_registers[0x400710/4];
static uint8_t irql;
#define MEM32(a) (*word(a))
static uint32_t *word(uint32_t va) { CHECK(va==ecx+0x1BC);return &head; }
#define XBOX_PTR(a) ((uintptr_t)(nv_registers))
#define RECOMP_EU_ENTER(a) ((void)0)
static uint8_t xbox_CurrentIrql(void) {return irql;}
static int xbox_gil_suspend(void) {++suspended;++head;return 2;} /* the other handler retires a flip */
static uint8_t xbox_KfRaiseIrql(uint8_t v) {uint8_t old=irql;irql=v;return old;}
static void xbox_gil_resume(int depth) {CHECK(depth==2 && irql==2);++resumed;}
static void xbox_KfLowerIrql(uint8_t v) {irql=v;}
static void xbox_Nv2aVblankTaken(void) {CHECK(irql==2);}
static void sub_002F23A0_gen(void) {CHECK(irql==2);++head;}
static void sub_002F2910_gen(void) {CHECK(irql==2 && trap_locked);++head;}
static void nv2a_pb_flip_retired(unsigned n) {++reported;retired+=n;}
static void nv2a_pb_trap_lock(void) {CHECK(irql==2 && !trap_locked);trap_locked=1;}
static void nv2a_pb_trap_unlock(void) {CHECK(trap_locked);trap_locked=0;}
static void nv2a_pb_trap_taken(void) {++trap_taken;}
'''
    irq_end = r'''
int main(void) {
    unsigned cases=0;
    for (unsigned handler=0;handler<2;++handler)
    for (unsigned dispatch=0;dispatch<2;++dispatch)
    for (unsigned wrap=0;wrap<2;++wrap) {
        head=wrap?UINT32_MAX:7;reported=retired=suspended=resumed=trap_locked=trap_taken=0;
        irql=dispatch?2:0;nv_registers[0x400108/4]=wrap;
        if(handler)sub_002F2910();else sub_002F23A0();
        CHECK(reported==1 && retired==1); /* never include the competing handler */
        CHECK(suspended==!dispatch && resumed==!dispatch && irql==(dispatch?2:0));
        CHECK(!trap_locked && trap_taken==(handler?wrap:0));++cases;
    }
    printf("PASS: %u EU flip cases, competing retirement and counter wrap\n",cases);
}
'''
    work = ROOT / 'build-eu'
    work.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='eu-frontend-', dir=work) as name:
        folder = Path(name).resolve()
        run(args.cc, folder, 'eeprom', prefix + constants + '\n' + api + suffix)
        common_rate = '#include "' + (ROOT/'src/frame_rate.c').as_posix() + '"\n'
        run(args.cc, folder, 'irq', irq + common_rate + wrappers + irq_end)
        if args.gen_dir and args.xbe:
            # Private generated game code and owner-provided data are read only;
            # the public test contains neither. Run the original selector logic.
            original = None
            for path in args.gen_dir.glob('recomp_*.c'):
                text = path.read_text()
                if 'void sub_001C1D80(void)' in text:
                    original = function(text, 'sub_001C1D80')
                    break
            if not original:
                raise ValueError('EU generated language selector missing')
            xbe = args.xbe.read_bytes()
            base = struct.unpack_from('<I', xbe, 0x104)[0]
            count, headers = struct.unpack_from('<II', xbe, 0x11C)
            words = None
            for index in range(count):
                va, size, raw, raw_size = struct.unpack_from('<IIII', xbe, headers-base+index*0x38+4)
                if va <= 0x1C1DBC and 0x1C1DD4 <= va+raw_size:
                    words = struct.unpack_from('<6I', xbe, raw+0x1C1DBC-va)
                    break
            if not words:
                raise ValueError('EU language jump table missing')
            probe = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static uint32_t eax,esp,g_ebp,g_seh_ebp,language,stack[4096],table[6];
#define RECOMP_PREEMPT() ((void)0)
#define RECOMP_ABI_CALL(va,fn) fn()
#define PUSH32(s,v) do {(s)-=4;stack[(s)/4]=(v);}while(0)
#define MEM32(va) table[((va)-0x1C1DBC)/4]
#define CMP_EQ(a,b) ((a)==(b))
#define CMP_A(a,b) ((uint32_t)(a)>(uint32_t)(b))
#define RECOMP_ITAIL(va) do {fprintf(stderr,"bad jump %x",va);exit(2);}while(0)
static void sub_0017A3F0(void){eax=1;esp+=4;}
static void sub_0021B4A1(void){eax=language;esp+=4;}
'''
            probe_end = r'''
int main(int argc,char **argv) {
    if(argc!=7)return 2;
    for(int i=0;i<6;++i)table[i]=(uint32_t)strtoul(argv[i+1],0,16);
    language=3;esp=0x1000;sub_001C1D80();
    if(eax!=2 || esp!=0x1004)return 2; /* DD220 -> DD100 -> German callback DD130 */
    puts("PASS: actual retail EU selector exposes German for Xbox console language 3");
}
'''
            run(args.cc, folder, 'retail-selector', probe + original + probe_end, *(format(w,'x') for w in words))

if __name__ == '__main__':
    main()
