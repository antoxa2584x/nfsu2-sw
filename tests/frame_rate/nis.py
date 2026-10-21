"""Exercise the US ISR's actual NIS guard and shared flip policy without an XBE."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='clang')
    args = parser.parse_args()
    source = (ROOT/'src/recomp_manual.c').read_text(encoding='utf8')
    code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "frame_rate.h"
static uint8_t guest[4u<<20];
static uint32_t mock_nv[0x400710/4], ecx=0x1000, calls;
#define MEM32(a) (*(uint32_t *)(guest+(a)))
#define XBOX_PTR(a) ((a)==0xFD000000u?(void *)mock_nv:(void *)(guest+(a)))
static uint8_t d3d_isr_enter(void){return 0;}
static void d3d_isr_leave(uint8_t old){(void)old;}
static void nv2a_pb_trap_lock(void){}
static void nv2a_pb_trap_unlock(void){}
static void nv2a_pb_trap_taken(void){}
static void nv2a_pb_flip_retired(uint32_t n){(void)n;}
static void sub_002F22F0_gen(void){calls++;}
'''
    code += function(source, 'static int nis_playing(void)') + '\n'
    code += function(source, 'void sub_002F22F0(void)') + '\n'
    code += r'''
int main(int argc,char **argv){
 unsigned count=0,capped=argc>1,nis_on=!getenv("NFSU2_NIS_CAP");
 for(unsigned playing=0;playing<8;playing++){
  MEM32(0x38DBB0)=playing&1;MEM32(0x38DBC8)=playing&2;MEM32(0x38DBE0)=playing&4;
  for(unsigned source=0;source<2;source++)
  for(unsigned method=0;method<2;method++)
  for(unsigned interval=0;interval<8;interval++)
  for(unsigned op=0;op<32;op++){
   uint32_t data=0xA5B6C900u|(interval<<5)|op,expected=data;
   mock_nv[0x400108/4]=source;mock_nv[0x400704/4]=0x100+method;mock_nv[0x400708/4]=data;
   if(!capped && !(playing && nis_on) && source && !method && op==1 && interval>1)
    expected=(data&~(7u<<5))|(1u<<5);
   sub_002F22F0();assert(mock_nv[0x400708/4]==expected);count++;
  }
 }
 assert(calls==count);printf("PASS: %u US ISR words, NIS %s, cap %s\n",count,nis_on?"on":"off",capped?"30":"60");
}
'''
    build = ROOT/'build-eu'
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='nis-', dir=build) as directory:
        work = Path(directory)
        src, exe = work/'test.c', work/'test.exe'
        src.write_text(code, encoding='utf8')
        subprocess.run([args.cc, '-O2', '-I'+str(ROOT/'src'), str(src),
                        str(ROOT/'src/frame_rate.c'), '-o', str(exe)], check=True)
        env = os.environ.copy()
        env.pop('NFSU2_FPS_CAP', None)
        env.pop('NFSU2_NIS_CAP', None)
        for extra in [{}, {'NFSU2_NIS_CAP':'0'}]:
            subprocess.run([str(exe)], env=env|extra, check=True)
            subprocess.run([str(exe), '30'], env=env|extra|{'NFSU2_FPS_CAP':'30'}, check=True)


if __name__ == '__main__':
    main()
