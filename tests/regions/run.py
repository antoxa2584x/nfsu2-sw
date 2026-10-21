"""Exercise the real region detector in both build modes; retail input is optional."""
from pathlib import Path
import argparse,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--cc',default='clang')
    ap.add_argument('--eu-xbe',type=Path);args=ap.parse_args()
    work=ROOT/'build-eu';work.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='region-tests-',dir=work) as name:
        directory=Path(name).resolve();assert directory.is_relative_to(work.resolve())
        for region in ['US','EU']:
            exe=directory/('test-'+region+'.exe')
            subprocess.run([args.cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-DNFSU2_BUILD_'+region+'=1',
                str(ROOT/'src/game_region.c'),str(ROOT/'tests/regions/test_main.c'),'-o',str(exe)],check=True)
            subprocess.run([str(exe),*([str(args.eu_xbe.resolve())] if args.eu_xbe else [])],check=True)
if __name__=='__main__':main()
