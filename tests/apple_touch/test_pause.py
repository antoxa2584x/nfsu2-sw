"""Exercise the production Apple pause gate with editor/background events."""
from pathlib import Path
import os
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = (root / 'src/apple/apple_main.c').read_text()
start = source.index('static pthread_mutex_t s_fg_lock')
end = source.index('#if defined(NFSU2_METAL)', start)
# The gate is tested separately from the guest interpreter: real pthread
# signalling/locking and event handlers, with guest-lock/audio observers.
prefix = r'''
#include <SDL.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <assert.h>
#include <time.h>
static int audioPaused;
void xa2_pause(int on) { audioPaused = on; }
void xbox_gil_enter(void) {}
void xbox_gil_leave(void) {}
'''
suffix = r'''
static atomic_int returned;
static void *wait_gate(void *unused) { (void)unused; nv2a_vk_host_gate(); atomic_store(&returned, 1); return NULL; }
int main(void)
{
    SDL_Event background = { .type = SDL_APP_WILLENTERBACKGROUND };
    SDL_Event foreground = { .type = SDL_APP_DIDENTERFOREGROUND };
    struct timespec delay = {0, 10000000};
    pthread_t thread;
    ios_controls_editor_pause(1); assert(audioPaused);
    pthread_create(&thread, NULL, wait_gate, NULL);
    app_events(NULL, &background);
    ios_controls_editor_pause(0); assert(audioPaused);
    nanosleep(&delay, NULL); assert(!atomic_load(&returned));
    app_events(NULL, &foreground); assert(!audioPaused);
    pthread_join(thread, NULL); assert(atomic_load(&returned));
    atomic_store(&returned, 0);
    ios_controls_editor_pause(1);
    app_events(NULL, &background); app_events(NULL, &foreground);
    assert(audioPaused); // foreground must not resume an open editor
    pthread_create(&thread, NULL, wait_gate, NULL);
    nanosleep(&delay, NULL); assert(!atomic_load(&returned));
    ios_controls_editor_pause(0); assert(!audioPaused);
    pthread_join(thread, NULL); assert(atomic_load(&returned));
    puts("Editor/background pause ownership and real pthread gate: passed");
}
'''
with tempfile.TemporaryDirectory() as directory:
    code = Path(directory) / 'pause.c'; exe = Path(directory) / 'pause'
    code.write_text(prefix + source[start:end] + suffix)
    include = os.environ.get('SDL_INCLUDE', '/opt/homebrew/include/SDL2')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-pthread', '-I' + include, str(code), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
