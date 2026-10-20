/*
 * Steam Deck (Linux, x86-64): detection, defaults and clock reporting.
 *
 * The Deck is a Linux PC, so the desktop build runs on it as is; what
 * differs is the setting:
 *
 *   - controls come from Steam Input (an Xbox-layout pad: labels and
 *     positions agree, LB/RB = White/Black like the Xbox 360's backward
 *     compatibility), and rumble goes to the Deck's haptics through it;
 *     xinput_device.c does both for any SDL pad.
 *   - the screen is 1280x800: fullscreen by default (RECOMP_FULLSCREEN=1;
 *     in Game Mode gamescope does that anyway), the 16:9 picture filled to
 *     the screen's height with the sides cut (RECOMP_PRESENT_FILL=1),
 *     rendered at 2.5x (RECOMP_GL_SCALE=2.5 until the option is changed).
 *   - draw distance x1.6 (NFSU2_DRAW_DISTANCE, main.c / recomp_manual.c).
 *   - clocks belong to Steam: Quick Access -> Performance sets the TDP
 *     limit, a manual GPU clock and (SteamOS 3.5+) the CPU's scaling limit,
 *     through root-only sysfs files. Stock limits already let the CPU boost
 *     to 3.5 GHz, so there is nothing for the game to raise, and writing
 *     those files would fight the user's own settings (the Switch build
 *     backs off from sys-clk/Horizon OC the same way). So NFSU2_CPU_MHZ /
 *     NFSU2_GPU_MHZ / NFSU2_MEM_MHZ are not acted on here: the clocks are
 *     read and logged, and logged again whenever Steam changes them
 *     ("[clock] ..." lines), next to the frame rate.
 *
 * Detection: DMI board vendor "Valve" with product Jupiter (LCD) or Galileo
 * (OLED), or SteamDeck=1 (Steam sets it in Game Mode). NFSU2_DECK=0/1
 * overrides.
 */
#if !defined(_WIN32) && !defined(__SWITCH__)

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int s_deck = -1;
static char s_model[32];

static int read_line(const char *path, char *out, size_t cap)
{
    FILE *f = fopen(path, "r");
    size_t n;
    if (!f)
        return 0;
    if (!fgets(out, (int)cap, f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    n = strlen(out);
    while (n && (out[n - 1] == '\n' || out[n - 1] == ' '))
        out[--n] = 0;
    return 1;
}

static long read_long(const char *path)
{
    char b[64];
    return read_line(path, b, sizeof b) ? atol(b) : -1;
}

int nfsu2_is_steam_deck(void)
{
    const char *e;
    char vendor[64], product[64];

    if (s_deck >= 0)
        return s_deck;
    s_deck = 0;
    if (read_line("/sys/class/dmi/id/board_vendor", vendor, sizeof vendor)
            && strcmp(vendor, "Valve") == 0
            && read_line("/sys/class/dmi/id/product_name", product, sizeof product)) {
        if (strcmp(product, "Jupiter") == 0) {
            s_deck = 1;
            strcpy(s_model, "LCD");
        } else if (strcmp(product, "Galileo") == 0) {
            s_deck = 1;
            strcpy(s_model, "OLED");
        }
    }
    e = getenv("SteamDeck");
    if (!s_deck && e && e[0] == '1') {
        s_deck = 1;
        strcpy(s_model, "SteamDeck=1");
    }
    e = getenv("NFSU2_DECK");
    if (e && (e[0] == '0' || e[0] == '1')) {
        s_deck = e[0] == '1';
        if (s_deck && !s_model[0])
            strcpy(s_model, "NFSU2_DECK=1");
    }
    return s_deck;
}

/* ---- clocks (read only) ---------------------------------------------- */

/* amdgpu's sysfs directory (…/device) and its hwmon, found once. */
static char s_gpu_dev[256];
static char s_gpu_hwmon[256];

static void find_gpu(void)
{
    DIR *d;
    struct dirent *de;
    char p[512], v[64];

    if ((d = opendir("/sys/class/drm"))) {
        while ((de = readdir(d))) {
            if (strncmp(de->d_name, "card", 4) || strchr(de->d_name, '-'))
                continue;
            snprintf(p, sizeof p, "/sys/class/drm/%s/device/vendor", de->d_name);
            if (read_line(p, v, sizeof v) && strcmp(v, "0x1002") == 0) {
                snprintf(s_gpu_dev, sizeof s_gpu_dev, "/sys/class/drm/%s/device",
                         de->d_name);
                break;
            }
        }
        closedir(d);
    }
    if ((d = opendir("/sys/class/hwmon"))) {
        while ((de = readdir(d))) {
            if (de->d_name[0] == '.')
                continue;
            snprintf(p, sizeof p, "/sys/class/hwmon/%s/name", de->d_name);
            if (read_line(p, v, sizeof v) && strcmp(v, "amdgpu") == 0) {
                snprintf(s_gpu_hwmon, sizeof s_gpu_hwmon, "/sys/class/hwmon/%s",
                         de->d_name);
                break;
            }
        }
        closedir(d);
    }
}

typedef struct {
    long cpu_max_mhz;      /* scaling_max_freq: the limit Steam (or the user) set */
    long cpu_cur_mhz;      /* average of the cores' current clocks */
    long gpu_mhz;          /* pp_dpm_sclk's active level */
    long gpu_manual;       /* power_dpm_force_performance_level == manual */
    long tdp_w;            /* hwmon power1_cap */
} Clocks;

static void read_clocks(Clocks *c)
{
    char p[512], line[128];
    long sum = 0, n = 0, f;
    int i;
    FILE *fp;

    memset(c, 0, sizeof *c);
    c->cpu_max_mhz = read_long("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq");
    if (c->cpu_max_mhz > 0)
        c->cpu_max_mhz /= 1000;
    for (i = 0; i < 64; i++) {
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
        if ((f = read_long(p)) < 0)
            break;
        sum += f / 1000;
        n++;
    }
    c->cpu_cur_mhz = n ? sum / n : -1;

    c->gpu_mhz = -1;
    if (s_gpu_dev[0]) {
        snprintf(p, sizeof p, "%s/pp_dpm_sclk", s_gpu_dev);
        if ((fp = fopen(p, "r"))) {
            while (fgets(line, sizeof line, fp)) {
                char *colon = strchr(line, ':');
                if (colon && strchr(line, '*'))
                    c->gpu_mhz = atol(colon + 1);
            }
            fclose(fp);
        }
        snprintf(p, sizeof p, "%s/power_dpm_force_performance_level", s_gpu_dev);
        c->gpu_manual = read_line(p, line, sizeof line) && strcmp(line, "manual") == 0;
    }
    c->tdp_w = -1;
    if (s_gpu_hwmon[0]) {
        snprintf(p, sizeof p, "%s/power1_cap", s_gpu_hwmon);
        if ((f = read_long(p)) > 0)
            c->tdp_w = (f + 500000) / 1000000;
    }
}

static void print_clocks(const char *why, const Clocks *c)
{
    printf("[clock] %s: CPU %ld MHz (limit %ld), GPU %ld MHz%s, TDP limit %ld W\n",
           why, c->cpu_cur_mhz, c->cpu_max_mhz, c->gpu_mhz,
           c->gpu_manual ? " (manual)" : "", c->tdp_w);
    fflush(stdout);
}

/* Logs the limits whenever Steam's Performance settings change them, and
 * the running clocks once a minute (they move with load). */
static void *clock_watch(void *arg)
{
    Clocks last, now;
    unsigned ticks = 0;

    (void)arg;
    read_clocks(&last);
    for (;;) {
        struct timespec ts = { 2, 0 };
        nanosleep(&ts, NULL);
        read_clocks(&now);
        if (now.cpu_max_mhz != last.cpu_max_mhz || now.tdp_w != last.tdp_w
                || now.gpu_manual != last.gpu_manual
                || (now.gpu_manual && now.gpu_mhz != last.gpu_mhz)) {
            print_clocks("changed in Steam's Performance settings", &now);
            ticks = 0;
        } else if (++ticks >= 30) {
            print_clocks("now", &now);
            ticks = 0;
        }
        last = now;
    }
    return NULL;
}

/* Called first thing in game_main: environment defaults, before anything
 * reads them. An explicit setting (environment, nfsu2x_env.txt through the
 * launcher) always wins. */
void nfsu2_deck_boot(void)
{
    static const char *const clock_vars[] = {
        "NFSU2_CPU_MHZ", "NFSU2_GPU_MHZ", "NFSU2_MEM_MHZ",
    };
    const char *e;
    Clocks c;
    pthread_t th;
    unsigned i;

    if (!nfsu2_is_steam_deck())
        return;
    e = getenv("SteamGamepadUI");
    printf("[deck] Steam Deck (%s), %s\n", s_model,
           e && e[0] == '1' ? "Game Mode" : "desktop");

    setenv("RECOMP_FULLSCREEN", "1", 0);
    /* 16:9 on the 1280x800 screen: full height, sides cut (inside the 4:3
     * area the HUD keeps to), no bars above and below. */
    setenv("RECOMP_PRESENT_FILL", "1", 0);
    /* 2.5x: the 640x480 title at 1600x1200 (square pixels: 2133x1200),
     * supersampled down to the screen. Options -> Video -> Resolution Scale
     * still overrides it once changed (saved as scale=). */
    setenv("RECOMP_GL_SCALE", "2.5", 0);
    /* Periodic runtime chatter (kernel summaries, GPU stats) off, as on the
     * Switch; RECOMP_QUIET=0 in nfsu2x_env.txt brings it back. */
    setenv("RECOMP_QUIET", "1", 0);
    /* The wording on the Deck: Black = RB in the replay legend. */
    setenv("NFSU2_DECK_TEXT", "1", 0);

    for (i = 0; i < sizeof clock_vars / sizeof clock_vars[0]; i++)
        if ((e = getenv(clock_vars[i])) && e[0])
            printf("[clock] %s=%s ignored: on the Deck clocks are set in Steam "
                   "(Quick Access -> Performance)\n", clock_vars[i], e);

    find_gpu();
    read_clocks(&c);
    print_clocks("at boot", &c);
    e = getenv("NFSU2_CLOCK_LOG");
    if (!(e && e[0] == '0') && pthread_create(&th, NULL, clock_watch, NULL) == 0)
        pthread_detach(th);
}

#endif
