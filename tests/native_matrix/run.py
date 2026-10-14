"""Check common native matrix arithmetic and overlapping output buffers."""
from pathlib import Path
import argparse,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--cc',default='clang');args=ap.parse_args()
    work=ROOT/'build-eu';work.mkdir(exist_ok=True)
    code=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "native_matrix.h"
int main(void)
{
    float a[20],b[16],out[16];
    const float expected[16]={250,260,270,280,618,644,670,696,986,1028,1070,1112,1354,1412,1470,1528};
    for(int i=0;i<16;i++){a[i]=(float)(i+1);b[i]=(float)(i+17);}
    mat4_mul(out,a,b);assert(!memcmp(out,expected,sizeof out));
    mat4_mul(a,a,b);assert(!memcmp(a,expected,sizeof out));
    for(int i=0;i<16;i++)a[i]=(float)(i+1);
    mat4_mul(b,a,b);assert(!memcmp(b,expected,sizeof out));
    for(int i=0;i<16;i++)b[i]=(float)(i+17);
    mat4_mul(a+1,a,b);assert(!memcmp(a+1,expected,sizeof out));
    puts("PASS: shared US/EU matrix multiply, output aliasing and partial overlap");
}
'''
    with tempfile.TemporaryDirectory(prefix='matrix-',dir=work) as name:
        directory=Path(name).resolve();assert directory.is_relative_to(work.resolve())
        src=directory/'matrix.c';exe=directory/'matrix.exe';src.write_text(code,encoding='utf-8')
        subprocess.run([args.cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
