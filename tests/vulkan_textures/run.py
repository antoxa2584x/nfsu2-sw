"""Run asset-free texture and shared-depth readbacks on an actual Vulkan GPU."""
from pathlib import Path
import argparse,os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--cc',default='clang');ap.add_argument('--cxx',default='clang++')
for name in ['vulkan-include','vulkan-lib','glslang-include','glslang-lib','glslang-resources-lib']:ap.add_argument('--'+name,required=True)
ap.add_argument('--baseline',type=Path,help='Optional old renderer; each regression must fail with exit 7')
args=ap.parse_args();env=os.environ.copy();env['PATH']=str(Path(args.cc).resolve().parent)+os.pathsep+env.get('PATH','')
build=ROOT/'build-eu';build.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='vulkan-textures-',dir=build) as folder:
 work=Path(folder);shader=[]
 def run(*cmd):subprocess.run(list(map(str,cmd)),env=env,check=True)
 for name in ['gl_vsh','gl_psh']:
  obj=work/(name+'.o');run(args.cc,'-O2','-c',ROOT/('xboxrecomp/src/nv2a_gl/'+name+'.c'),'-o',obj);shader.append(obj)
 variants=[('fixed',ROOT/'xboxrecomp/src/nv2a_vk/nv2a_vk.c')]
 if args.baseline:variants.insert(0,('baseline',args.baseline.resolve()))
 for variant,source in variants:
  obj=work/(variant+'.o');exe=work/(variant+'.exe')
  run(args.cc,'-O2',*(['-DTEST_RUNTIME_MIPS'] if variant=='fixed' else []),'-DNV2A_SOURCE="'+source.as_posix()+'"','-I'+str(HERE),'-I'+args.vulkan_include,
   '-I'+args.glslang_include,'-I'+str(ROOT/'xboxrecomp/src/nv2a_vk'),'-c',HERE/'fixture.c','-o',obj)
  run(args.cxx,obj,*shader,args.vulkan_lib,args.glslang_lib,args.glslang_resources_lib,
   *(['-lwinpthread'] if os.name=='nt' else ['-lpthread']),'-o',exe)
  cases=['pitch','levels','mip-change','palette','depth','surface-aa']
  if variant=='fixed':cases.extend(['runtime-mips','lod-clamp','square-size','alpha-variants'])
  for case in cases:
   r=subprocess.run([str(exe),case],env=env,capture_output=True,text=True,timeout=30)
   assert r.returncode==(7 if variant=='baseline' else 0),(variant,case,r.returncode,r.stdout,r.stderr)
   print(variant+': '+case+' PASS')
