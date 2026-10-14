"""Check the shared movie converter's pixels and guest register postconditions."""
from pathlib import Path
import argparse,os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--cc',default='clang');args=ap.parse_args()
    code=r'''
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
static unsigned char memory[65536];
static uint32_t eax,ecx,edx,esp;
typedef union { int16_t w[4]; uint64_t q; } Mmx;
static Mmx mm0,mm1,mm2,mm3;
#define XBOX_PTR(va) ((uintptr_t)(memory+(va)))
#define MEM8(va) (memory[(va)])
#define MEM32(va) (*(uint32_t *)(memory+(va)))
#include "native_movie_row.h"
static unsigned generated_calls;
static void generated(void) { generated_calls++;esp+=4; }
int main(void)
{
    uint32_t y=0x2000,u=0x2100,v=0x2200,dst=0x3000;
    int16_t *ty=(void *)(memory+0x4000),*tu=(void *)(memory+0x5000),*tv=(void *)(memory+0x6000);
    const int16_t y0[4]={0,40,0,0},y1[4]={100,0,-500,-300},cu[4]={10,-10,100,255},cv[4]={20,0,300,0};
    memcpy(ty,y0,8);memcpy(ty+4,y1,8);memcpy(tu+8,cu,8);memcpy(tv+12,cv,8);
    memory[y]=0;memory[y+1]=1;memory[u]=2;memory[v]=3;
    esp=0x1000;
    MEM32(esp+4)=y;MEM32(esp+8)=u;MEM32(esp+12)=v;MEM32(esp+16)=dst;MEM32(esp+20)=dst+8;
    nfsu2_movie_row_override(generated,0x4000,0x5000,0x6000);
    const unsigned char expected[8]={30,30,255,255,130,0,0,0};
    assert(!memcmp(memory+dst,expected,8));
    assert(eax==0 && ecx==y+2 && edx==u+1 && esp==0x1004);
    assert(nfsu2_movie_row==dst && !memcmp(&mm0,expected,8));
    assert(mm3.w[2]==300 && mm2.w[2]==400 && mm1.w[2]==-100);
    assert(generated_calls==0);
    const int16_t yw[4]={32760,-32760,30000,-30000},uw[4]={20,-20,10000,-10000};
    memset(tv,0,2048);memcpy(ty,yw,8);memcpy(ty+4,yw,8);memcpy(tu+8,uw,8);
    esp=0x1000;
    nfsu2_movie_row_override(generated,0x4000,0x5000,0x6000);
    const unsigned char wrapped[8]={0,255,0,255,0,255,0,255};
    assert(!memcmp(memory+dst,wrapped,8));
    esp=0x1000;MEM32(esp+20)=dst+9;
    nfsu2_movie_row_override(generated,0x4000,0x5000,0x6000);
    assert(generated_calls==1 && esp==0x1004);
    puts("PASS: shared US/EU movie pixels, signed wrap, MMX/GPR state and fallback");
}
'''
    work=ROOT/'build-eu';work.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='movie-row-',dir=work) as name:
        directory=Path(name).resolve();assert directory.is_relative_to(work.resolve())
        src=directory/'row.c';src.write_text(code,encoding='utf-8')
        env=os.environ.copy();env['RECOMP_NATIVE']='1';env['RECOMP_NATIVE_CHECK']='0'
        for scalar in [False,True]:
            exe=directory/('scalar.exe' if scalar else 'simd.exe')
            subprocess.run([args.cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',*(['-U__SSE2__'] if scalar else []),
                '-I',str(ROOT/'src'),str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],env=env,check=True)
if __name__=='__main__':main()
