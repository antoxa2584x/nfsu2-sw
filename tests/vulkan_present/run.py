"""Exercise the real shared renderer with delayed presentation and API errors."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', default='clang')
    ap.add_argument('--cxx', default='clang++')
    ap.add_argument('--vulkan-include', required=True)
    ap.add_argument('--glslang-include', required=True)
    ap.add_argument('--glslang-lib', required=True)
    ap.add_argument('--glslang-resources-lib', required=True)
    ap.add_argument('--baseline', type=Path, help='Optional previous renderer to expose early semaphore reuse')
    args = ap.parse_args()
    env = os.environ.copy()
    env['PATH'] = str(Path(args.cc).resolve().parent)+os.pathsep+env.get('PATH', '')
    build = ROOT/'build-eu'
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='nfsu2-present-', dir=build) as directory:
        work = Path(directory)
        def checked(*cmd):
            subprocess.run(list(map(str, cmd)), check=True, env=env)
        shaders = []
        for name in ['gl_vsh', 'gl_psh']:
            obj = work/(name+'.o')
            checked(args.cc, '-O2', '-c', ROOT/('xboxrecomp/src/nv2a_gl/'+name+'.c'), '-o', obj)
            shaders.append(obj)
        sources = [('fixed', ROOT/'xboxrecomp/src/nv2a_vk/nv2a_vk.c')]
        if args.baseline:
            sources.insert(0, ('baseline', args.baseline.resolve()))
        for name, source in sources:
            fixture = work/(name+'-fixture.o')
            checked(args.cc, '-O2', *(['-DHAVE_FPS_OVERLAY'] if name=='fixed' else []),
                    '-I'+str(HERE), '-I'+args.vulkan_include,
                    '-I'+str(ROOT/'xboxrecomp/src/kernel'), '-c', HERE/'fixture.c', '-o', fixture)
            obj, exe = work/(name+'.o'), work/(name+'.exe')
            checked(args.cc, '-O2', '-D__SWITCH__', '-I'+str(HERE),
                    '-I'+args.vulkan_include, '-I'+args.glslang_include,
                    '-I'+str(ROOT/'xboxrecomp/src/nv2a_vk'), '-c', source, '-o', obj)
            checked(args.cxx, obj, fixture, *shaders, args.glslang_lib,
                    args.glslang_resources_lib, *(['-lwinpthread'] if os.name=='nt' else ['-lpthread']), '-o', exe)
            result = subprocess.run([str(exe)], env=env, capture_output=True, text=True)
            assert result.returncode == (4 if name=='baseline' else 0), (name, result.stdout, result.stderr)
            print(name+': '+result.stdout.strip())
            if name=='baseline':
                continue
            checked(exe, 'suboptimal')
            checked(exe, 'fps-toggle')
            env['NFSU2_FPS_OVERLAY'] = '1'
            checked(exe, 'fps-env')
            del env['NFSU2_FPS_OVERLAY']
            for fault in ['vkAcquireNextImageKHR', 'vkQueueSubmit', 'vkQueuePresentKHR',
                          'vkWaitForFences', 'vkResetFences', 'vkResetCommandPool',
                          'vkBeginCommandBuffer', 'vkEndCommandBuffer', 'vkCreateSemaphore',
                          'acquire_outdated', 'present_outdated', 'bad_index']:
                result = subprocess.run([str(exe), fault], env=env, capture_output=True, text=True)
                assert result.returncode != 0 and 'FATAL runtime operation' in result.stderr, (fault, result.stdout, result.stderr)
            print('PASS: delayed presentation, suboptimal surfaces and 12 injected failures')


if __name__ == '__main__':
    main()
