"""Check the shared US/EU flip policy without retail inputs."""
from pathlib import Path
import argparse,os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
ap=argparse.ArgumentParser();ap.add_argument('--cc',default='clang');args=ap.parse_args()
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "frame_rate.h"
int main(int argc,char **argv) {
 unsigned capped=argc>1, count=0;
 for(unsigned source=0;source<2;source++)
 for(unsigned method=0;method<3;method++)
 for(unsigned interval=0;interval<8;interval++)
 for(unsigned op=0;op<32;op++) {
  uint32_t addr=method==0?0x100:method==1?0x101:0x12340100;
  uint32_t data=0xA5B6C900u|(interval<<5)|op;
  uint32_t result=nfsu2_flip_trap_data(source,addr,data),expected=data;
  if(!capped && source && (addr&0xFFFF)==0x100 && op==1 && interval>1)
   expected=(data&~(7u<<5))|(1u<<5);
  assert(result==expected);count++;
 }
 printf("PASS: %u shared flip words, cap %s\n",count,capped?"30":"60");
}
'''
build=ROOT/'build-eu';build.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='frame-rate-',dir=build) as folder:
 p=Path(folder);src=p/'test.c';src.write_text(code);exe=p/'test.exe'
 subprocess.run([args.cc,'-O2','-I'+str(ROOT/'src'),str(src),str(ROOT/'src/frame_rate.c'),'-o',str(exe)],check=True)
 env=os.environ.copy();env.pop('NFSU2_FPS_CAP',None)
 subprocess.run([str(exe)],env=env,check=True)
 subprocess.run([str(exe),'30'],env=env|{'NFSU2_FPS_CAP':'30'},check=True)
