/*
 * ios_host.c -- where things live inside the iOS app sandbox, set up before
 * the title boots. Plain C: the sandbox's HOME is the app container, so no
 * Objective-C is needed to find Documents/ and Library/.
 *
 *   Documents/game/            the extracted disc (copied in with the Files
 *                              app or Finder); NFSU2_GAME_DIR
 *   Documents/nfsu2x_env.txt   KEY=VALUE per line, '#' comments -- the
 *                              Switch build's settings file, same format
 *   Documents/nfsu2x_log.txt   stdout + stderr (device only; the simulator
 *                              keeps its console)
 *   Library/Application Support/xboxrecomp/   save partitions (XDG_DATA_HOME)
 *   Library/Caches/            working directory: xbox_kernel.log and the
 *                              renderer caches (progcache.bin, vk*.bin)
 *
 * Settings from the file win over the defaults set here; the simulator's
 * launch environment (SIMCTL_CHILD_*) arrives before either and is kept
 * unless the file names the same key.
 *
 * For measurements driven from the Mac when the device's file service will
 * not move files (it can hang while the log grows), two launch variables
 * (devicectl ... process launch --console, DEVICECTL_CHILD_*):
 *   NFSU2_LOG_CONSOLE=1   the log stays on stdout/stderr (to the console)
 *   NFSU2_ENV_FILE=0      nfsu2x_env.txt is not read: the launch
 *                         environment is the whole configuration
 */
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void path_join(char *out, size_t cap, const char *a, const char *b)
{
    snprintf(out, cap, "%s/%s", a, b);
}

/* KEY=VALUE lines; blank lines and '#' comments skipped, spaces around the
 * key and value trimmed. */
static int read_env_file(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[1024];
    int n = 0;

    if (!f)
        return -1;
    while (fgets(line, sizeof line, f)) {
        char *k = line, *eq, *v, *end;
        while (*k == ' ' || *k == '\t')
            k++;
        if (*k == '#' || *k == '\n' || *k == '\r' || !*k)
            continue;
        eq = strchr(k, '=');
        if (!eq)
            continue;
        *eq = 0;
        v = eq + 1;
        for (end = eq; end > k && (end[-1] == ' ' || end[-1] == '\t'); end--)
            end[-1] = 0;
        while (*v == ' ' || *v == '\t')
            v++;
        for (end = v + strlen(v); end > v && (end[-1] == '\n' || end[-1] == '\r' ||
                                             end[-1] == ' ' || end[-1] == '\t'); end--)
            end[-1] = 0;
        if (*k) {
            setenv(k, v, 1);
            n++;
        }
    }
    fclose(f);
    return n;
}

void ios_host_setup(void)
{
    const char *home = getenv("HOME");
    char docs[1024], lib[1024], support[1024], caches[1024], path[1200];
    int nenv;

    if (!home || !*home)
        home = ".";
    path_join(docs, sizeof docs, home, "Documents");
    path_join(lib, sizeof lib, home, "Library");
    path_join(support, sizeof support, lib, "Application Support");
    path_join(caches, sizeof caches, lib, "Caches");
    mkdir(docs, 0755);
    mkdir(support, 0755);
    mkdir(caches, 0755);

#if !TARGET_OS_SIMULATOR
    {
        const char *c = getenv("NFSU2_LOG_CONSOLE");
        path_join(path, sizeof path, docs, "nfsu2x_log.txt");
        if (!(c && *c == '1') && freopen(path, "w", stderr))
            dup2(fileno(stderr), fileno(stdout));
    }
#endif

    {
        const char *e = getenv("NFSU2_ENV_FILE");
        path_join(path, sizeof path, docs, "nfsu2x_env.txt");
        nenv = (e && *e == '0') ? -1 : read_env_file(path);
    }

    setenv("NFSU2_FPS_LIMIT", "0", 0);
    setenv("RECOMP_GL_SCALE", "4", 0);
    path_join(path, sizeof path, docs, "game");
    setenv("NFSU2_GAME_DIR", path, 0);
    setenv("XDG_DATA_HOME", support, 0);
    if (chdir(caches) != 0)
        fprintf(stderr, "[ios] cannot enter %s: %s\n", caches, strerror(errno));

    fprintf(stderr, "[ios] container %s\n[ios] game %s, settings: %s\n", home,
            getenv("NFSU2_GAME_DIR"),
            nenv < 0 ? "no nfsu2x_env.txt" : nenv == 0 ? "nfsu2x_env.txt (empty)" : "nfsu2x_env.txt");
}

#endif /* TARGET_OS_IPHONE */
