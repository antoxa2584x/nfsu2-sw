"""Build and run the actual heap bootstrap with mocked loader/kernel services."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--cc',default='clang');args=ap.parse_args()
source=ROOT/'src/switch_heap.c';digest=hashlib.sha256(source.read_bytes()).hexdigest()
outroot=ROOT/'build-eu';outroot.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='heap-',dir=outroot) as name:
    output=Path(name).resolve();assert output.is_relative_to(outroot.resolve())
    exe=output/'test_switch_heap.exe'
    subprocess.run([args.cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(HERE/'stubs'),str(HERE/'test_main.c'),'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
    print(result.stdout,end='')
    assert output.is_relative_to(outroot.resolve())
assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
print('PASS actual source checksum:',digest)
