/* Port the existing US v0.5 Video options to the independently verified EU
 * XBE addresses. Only private copies of the Speedometer row vtable are used.
 * Original source: antoxa2584x/nfsu2-sw 48b1b2798d60f5deb80a189781d78643ac8f7f8a.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"
#include "../../video_recommendation.h"

#ifdef __SWITCH__
#define NFSU2_OPTIONS_FILE "sdmc:/switch/nfsu2x/nfsu2x_options.txt"
#else
#define NFSU2_OPTIONS_FILE "nfsu2x_options.txt"
#endif
#define FE_PAD_LEFT 0x9120409Eu
#define FE_PAD_RIGHT 0xB5971BF1u
#define ROW_VTABLE 0x0034F5F8u
#define ROW_VTABLE_SIZE 0x48u
#define ENVMAP_TABLE 0x003F3210u
/* The retail chooser writes the selected game language here before loading
 * its resources (0011EF40). This reads the choice; it never selects a row. */
int nfsu2_text_language_is_german(void)
{
    return MEM32(0x003A0E30u) == 2u;
}

#ifdef NFSU2_VULKAN
extern volatile int nv2a_vk_cube_maps;
extern volatile int nv2a_vk_scale_pct;
extern volatile int nv2a_vk_fps_overlay;
extern volatile int nv2a_vk_fxaa;
extern volatile int nv2a_vk_aniso;
extern volatile int nv2a_vk_square;
extern volatile int nv2a_vk_mips;
#endif
extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment);
extern void sub_000CE7B0_gen(void);
extern void sub_000DC4B0_gen(void);
extern void sub_000B5110_gen(void);
extern void sub_000B5170_gen(void);
extern void sub_0009B680_gen(void);

static const int s_scale_pct[] = {100, 150, 200, 250};
static const int s_aniso_lvl[] = {1, 2, 4, 8, 16};
static int s_refl = 1, s_scale_idx = 1, s_scale_saved;
static int s_fps;
/* Keep the initial EU picture equivalent to the original renderer.
 * Picture options remain available and the user's saved choices win. */
static int s_fxaa, s_aniso_idx, s_square;
static uint32_t s_video_screen, s_video_title, s_video_rows[6];
typedef struct {
    uint32_t label;
    const uint32_t *texts;
    int n;
    int *value;
    void (*changed)(void);
    uint32_t vtable;
} OptRow;
static void refl_changed(void)
{
#ifdef NFSU2_VULKAN
    nv2a_vk_cube_maps = s_refl;
#endif
}
#ifdef NFSU2_VULKAN
static void scale_changed(void)
{
    nv2a_vk_scale_pct = s_scale_pct[s_scale_idx];
    s_scale_saved = 1;
}
static void fps_changed(void)
{
    nv2a_vk_fps_overlay = s_fps;
}
static void picture_changed(void)
{
    nv2a_vk_fxaa = s_fxaa;
    nv2a_vk_aniso = s_aniso_lvl[s_aniso_idx];
    nv2a_vk_square = s_square;
    nv2a_vk_mips = s_aniso_idx != 0;
}
#endif
static const uint32_t s_refl_texts[] = {0x0000CCFAu, 0x0000063Cu};
static const uint32_t s_scale_texts[] = {0x2A2A4AB3u, 0x822E4E9Bu, 0xB55E7665u, 0xD3588630u};
static const uint32_t s_aniso_texts[] = {0x0000CCFAu, 0xB55E7665u, 0xA7A683ACu, 0x6F2C7417u, 0x62925361u};
static OptRow s_rows[] = {
    {0x8FE9288Eu, s_refl_texts, 2, &s_refl, refl_changed, 0},
#ifdef NFSU2_VULKAN
    {0x4AC50BCFu, s_scale_texts, 4, &s_scale_idx, scale_changed, 0},
    {0xA9A76BBAu, s_refl_texts, 2, &s_fps, fps_changed, 0},
    {0x4CD775ACu, s_refl_texts, 2, &s_fxaa, picture_changed, 0},
    {0x4E57D355u, s_aniso_texts, 5, &s_aniso_idx, picture_changed, 0},
    {0xA9C27DFDu, s_refl_texts, 2, &s_square, picture_changed, 0},
#endif
};
#define N_ROWS ((int)(sizeof s_rows / sizeof s_rows[0]))

/* Treat a damaged/edited options file as a failed setting, never an index. */
static int parsed_value(const char *s, long *value)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s)
        return 0;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')
        ++end;
    if (*end)
        return 0;
    *value = v;
    return 1;
}
void nfsu2_options_load(void)
{
    char line[128];
    FILE *f = fopen(NFSU2_OPTIONS_FILE, "r");
    int i;
    const char *fps_env = getenv("NFSU2_FPS_OVERLAY");
    s_fps = fps_env && !strcmp(fps_env, "1");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            long value;
            if (!strncmp(line, "reflections=", 12) && parsed_value(line + 12, &value)) {
                if (value == 0 || value == 1)
                    s_refl = (int)value;
            } else if (!strncmp(line, "fps_display=", 12) && parsed_value(line + 12, &value)) {
                if (value == 0 || value == 1)
                    s_fps = (int)value;
            } else if (!strncmp(line, "fxaa=", 5) && parsed_value(line + 5, &value)) {
                if (value == 0 || value == 1) s_fxaa = (int)value;
            } else if (!strncmp(line, "square=", 7) && parsed_value(line + 7, &value)) {
                if (value == 0 || value == 1) s_square = (int)value;
            } else if (!strncmp(line, "aniso=", 6) && parsed_value(line + 6, &value)) {
                for (i = 0; i < 5; ++i)
                    if (s_aniso_lvl[i] == value) s_aniso_idx = i;
            } else if (!strncmp(line, "scale=", 6) && parsed_value(line + 6, &value)) {
                for (i = 0; i < 4; ++i)
                    if (s_scale_pct[i] == value) {
                        s_scale_idx = i;
                        s_scale_saved = 1;
                    }
            }
        }
        fclose(f);
    }
    refl_changed();
#ifdef NFSU2_VULKAN
    fps_changed();
    picture_changed();
    {
        const char *e = getenv("RECOMP_GL_SCALE");
        if (!s_scale_saved && e && *e) {
            double k = strtod(e, NULL), best = 1e9;
            for (i = 0; i < 4; ++i) {
                double d = fabs(k * 100.0 - s_scale_pct[i]);
                if (d < best) { best = d; s_scale_idx = i; }
            }
        } else {
            nv2a_vk_scale_pct = s_scale_pct[s_scale_idx];
        }
    }
    fprintf(stderr, "[options] car reflections %s, resolution scale %d%%%s; FXAA %s, aniso %dx, square %s, FPS %s\n",
            s_refl ? "on" : "off", s_scale_pct[s_scale_idx],
            nv2a_vk_scale_pct ? "" : " (RECOMP_GL_SCALE)", s_fxaa ? "on" : "off",
            s_aniso_lvl[s_aniso_idx], s_square ? "on" : "off", s_fps ? "on" : "off");
#else
    fprintf(stderr, "[options] car reflections %s\n", s_refl ? "on" : "off");
#endif
}
static void options_save(void)
{
    FILE *f = fopen(NFSU2_OPTIONS_FILE, "w");
    if (!f) { fprintf(stderr, "[options] cannot write " NFSU2_OPTIONS_FILE "\n"); return; }
    fprintf(f, "reflections=%d\n", s_refl);
#ifdef NFSU2_VULKAN
    fprintf(f, "fps_display=%d\n", s_fps);
    fprintf(f, "fxaa=%d\naniso=%d\nsquare=%d\n", s_fxaa, s_aniso_lvl[s_aniso_idx], s_square);
#endif
    if (s_scale_saved)
        fprintf(f, "scale=%d\n", s_scale_pct[s_scale_idx]);
    if (fclose(f))
        fprintf(stderr, "[options] settings write failed\n");
}
static OptRow *opt_row(uint32_t obj)
{
    uint32_t vt;
    if (!obj || !(vt = MEM32(obj)))
        return NULL;
    for (int i = 0; i < N_ROWS; ++i)
        if (s_rows[i].vtable && s_rows[i].vtable == vt)
            return &s_rows[i];
    return NULL;
}
static void refl_views(void)
{
    if (s_refl)
        return;
    for (unsigned n = 0; n < 6; ++n) {
        uint32_t v = MEM32(ENVMAP_TABLE + n * 4);
        if (v)
            MEM8(v + 8) = 0;
    }
}

/* Do not leak the native row's extra calls into the generated caller's GPR
 * state. In particular, generated callees publish g_ebp/g_seh_ebp. */
typedef struct { uint32_t a,c,d,s,b,si,di,bp,seh; } GuestFrame;
static GuestFrame frame_save(void)
{
    GuestFrame f = {g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp};
    return f;
}
static void frame_restore(GuestFrame f, unsigned pop)
{
    g_eax=f.a;g_ecx=f.c;g_edx=f.d;g_esp=f.s+pop;g_ebx=f.b;
    g_esi=f.si;g_edi=f.di;g_ebp=f.bp;g_seh_ebp=f.seh;
}
#define EU_OPTION_CALL(va, fn, frame) do { \
    g_ebp = (frame).bp; g_seh_ebp = (frame).seh; \
    RECOMP_ABI_CALL((va), fn); \
} while (0)

static void pause_caption(uint32_t screen, uint32_t name, uint32_t label, GuestFrame f);

static int video_above_recommendation(void)
{
    return s_scale_pct[s_scale_idx] > 150 || s_fxaa || s_aniso_idx > 0 || s_square;
}

static void video_notice_update(GuestFrame f)
{
    if (!s_video_screen)
        return;
    int warning = video_above_recommendation();
    MEM32(s_video_screen + 0x2Cu) = warning ? NFSU2_VIDEO_WARNING_HELP : NFSU2_VIDEO_RECOMMENDED_HELP;
    PUSH32(g_esp, warning ? NFSU2_VIDEO_WARNING_TITLE : NFSU2_VIDEO_RECOMMENDED_TITLE);
    PUSH32(g_esp, s_video_title);
    PUSH32(g_esp, MEM32(s_video_screen + 4));
    PUSH32(g_esp, 0x000E6AC7u);
    EU_OPTION_CALL(0x001447F0u, sub_001447F0, f);
    g_esp += 8;
    PUSH32(g_esp, g_eax);
    PUSH32(g_esp, 0x000E6AD0u);
    EU_OPTION_CALL(0x00119020u, sub_00119020, f);
    g_esp += 8;
}

static void video_notice_begin(uint32_t screen, int pause, GuestFrame f)
{
    s_video_screen = screen;
    memset(s_video_rows, 0, sizeof s_video_rows);
    s_video_title = 0x42ADB44Cu;
    if (pause) {
        PUSH32(g_esp, 0x00350CC8u);
        PUSH32(g_esp, 0x000DAECCu);
        EU_OPTION_CALL(0x00116500u, sub_00116500, f);
        g_esp += 4;
        s_video_title = g_eax;
    }
}

/* The console Video page shows a fixed TV calibration panel below its
 * single original row. Extra rows occupy that same area. Hide only those
 * three decorative objects, through the engine's ordinary visibility API. */
static void video_calibration_hide(uint32_t screen, GuestFrame f)
{
    static const uint32_t objects[] = {
        0xE54C30BEu, /* ColorCal_Logo */
        0x8E1BEA84u, /* ColorCal_Blurb */
        0x608BB8C8u, /* Colour_Calibration_Backing */
    };
    uint32_t package = MEM32(screen + 4);
    if (!package)
        return;
    for (unsigned i = 0; i < sizeof objects / sizeof objects[0]; ++i) {
        PUSH32(g_esp, objects[i]);
        PUSH32(g_esp, package);
        PUSH32(g_esp, 0x000CE7F2u);
        EU_OPTION_CALL(0x001447A0u, sub_001447A0, f);
        g_esp += 8;
        PUSH32(g_esp, g_eax);
        PUSH32(g_esp, 0x000CE7F8u);
        EU_OPTION_CALL(0x00119110u, sub_00119110, f);
        g_esp += 4;
    }
    MEM8(screen + 0x150u) = 0;
}

static int video_rows_add(uint32_t screen, GuestFrame f)
{
    int added = 0;
    for (int i = 0; i < N_ROWS; ++i) {
        OptRow *o = &s_rows[i];
        if (!o->vtable) {
            /* Verify the retail EU row's dispatch before cloning it. */
            if (MEM32(ROW_VTABLE + 4) != 0x000B5110u ||
                MEM32(ROW_VTABLE + 12) != 0x000B5170u ||
                MEM32(ROW_VTABLE + 64) != 0x0012B910u) {
                fprintf(stderr, "[options] EU row vtable differs; extra rows unavailable\n");
                break;
            }
            o->vtable = xbox_ContiguousAlloc(ROW_VTABLE_SIZE, 16);
            if (!o->vtable) { fprintf(stderr, "[options] row vtable allocation failed\n"); break; }
            memcpy((void *)XBOX_PTR(o->vtable), (const void *)XBOX_PTR(ROW_VTABLE), ROW_VTABLE_SIZE);
        }
        PUSH32(g_esp, 0x54u);
        PUSH32(g_esp, 0x000CE905u);
        EU_OPTION_CALL(0x0017F940u, sub_0017F940, f);
        g_esp += 4;
        uint32_t row = g_eax;
        if (!row) { fprintf(stderr, "[options] row allocation failed\n"); break; }
        PUSH32(g_esp, 1);
        PUSH32(g_esp, screen + 0x120u);
        PUSH32(g_esp, screen + 0x118u);
        PUSH32(g_esp, screen + 0x110u);
        PUSH32(g_esp, screen + 0x100u);
        g_ecx = row;
        PUSH32(g_esp, 0x000CE93Du);
        EU_OPTION_CALL(0x0012B480u, sub_0012B480, f);
        MEM32(row) = o->vtable;
        s_video_rows[i] = row;
        PUSH32(g_esp, 1);
        PUSH32(g_esp, row);
        g_ecx = screen;
        PUSH32(g_esp, 0x000CE957u);
        EU_OPTION_CALL(0x0015B8C0u, sub_0015B8C0, f);
        ++added;
    }
    return added;
}

void sub_000CE7B0(void)
{
    uint32_t screen = g_ecx;
    sub_000CE7B0_gen();
    GuestFrame f = frame_save();
    video_notice_begin(screen, 0, f);
    if (video_rows_add(screen, f)) {
        video_calibration_hide(screen, f);
        video_notice_update(f);
    }
    frame_restore(f, 0);
}

/* Pause Options uses its own package and return path. Add the retail Video
 * category to that menu, then reuse the same row callbacks and saved values.
 * Keep the pause screen's +0x150 pointer intact: it is not the main-menu TV
 * calibration flag. */
void sub_000DA850(void)
{
    uint32_t screen = g_ecx;
    GuestFrame f = frame_save();
    RECOMP_EU_ENTER(0x000DA850u); RECOMP_PREEMPT();
    pause_caption(screen, 0x0034D46Cu, 0x1D7BB6C9u, f);
    pause_caption(screen, 0x00350C6Cu, 0x933DD695u, f);
    pause_caption(screen, 0x00350C5Cu, 0x8202C364u, f);
    /* The same order as Options in the garage: Audio, Video, Controller,
     * Gameplay. Reuse the retail item constructors and selection callbacks. */
    static const struct { uint32_t title, label, vtable, select; } items[] = {
        {0xE76CD783u, 0xF37AF144u, 0x0034F184u, 0x000B4550u},
        {0xE8E24508u, 0x8A006328u, 0x0034F18Cu, 0x000B4570u},
        {0xD0CF6EE1u, 0x4DF98FB2u, 0x0034F194u, 0x000B4590u},
        {0xF760EABEu, 0xD708EFEFu, 0x0034F19Cu, 0x000B45B0u},
    };
    for (unsigned i = 0; i < sizeof items / sizeof items[0]; ++i) {
        if (MEM32(items[i].vtable) != 0x0013DBE0u ||
            MEM32(items[i].vtable + 4) != items[i].select) {
            fprintf(stderr, "[options] EU pause category vtable differs\n");
            continue;
        }
        PUSH32(g_esp, 0x48u);
        PUSH32(g_esp, 0x000CE2EAu);
        EU_OPTION_CALL(0x0017F940u, sub_0017F940, f);
        g_esp += 4;
        uint32_t item = g_eax;
        if (!item) {
            fprintf(stderr, "[options] pause category allocation failed\n");
            continue;
        }
        PUSH32(g_esp, 0);
        PUSH32(g_esp, items[i].title);
        PUSH32(g_esp, items[i].label);
        g_ecx = item;
        PUSH32(g_esp, 0x000CE310u);
        EU_OPTION_CALL(0x0012C5C0u, sub_0012C5C0, f);
        MEM32(item) = items[i].vtable;
        uint32_t before = g_esp;
        PUSH32(g_esp, item);
        g_ecx = screen;
        g_ebp = f.bp; g_seh_ebp = f.seh;
        PUSH32(g_esp, 0x000DA9A3u);
        RECOMP_ICALL_SAFE_AT(MEM32(MEM32(screen) + 0x18u), before, 0x000DA9A0u);
    }

    PUSH32(g_esp, MEM32(screen + 4));
    PUSH32(g_esp, 0x000DA9B2u);
    EU_OPTION_CALL(0x00118560u, sub_00118560, f);
    g_esp += 4;
    uint32_t before = g_esp;
    PUSH32(g_esp, g_eax);
    g_ecx = screen + 0x50u;
    g_ebp = f.bp; g_seh_ebp = f.seh;
    PUSH32(g_esp, 0x000DA9BBu);
    RECOMP_ICALL_SAFE_AT(MEM32(MEM32(screen + 0x50u) + 0x1Cu), before, 0x000DA9B8u);

    before = g_esp;
    g_ecx = screen;
    g_ebp = f.bp; g_seh_ebp = f.seh;
    PUSH32(g_esp, 0x000DA9C2u);
    RECOMP_ICALL_SAFE_AT(MEM32(MEM32(screen) + 0x10u), before, 0x000DA9BFu);
    frame_restore(f, 4);
    fprintf(stderr, "[options] pause Video category added\n");
}

static void pause_caption(uint32_t screen, uint32_t name, uint32_t label, GuestFrame f)
{
    PUSH32(g_esp, label);
    PUSH32(g_esp, name);
    PUSH32(g_esp, 0x000DABB0u);
    EU_OPTION_CALL(0x00116500u, sub_00116500, f);
    PUSH32(g_esp, g_eax);
    PUSH32(g_esp, MEM32(screen + 4));
    PUSH32(g_esp, 0x000DABB7u);
    EU_OPTION_CALL(0x001447F0u, sub_001447F0, f);
    g_esp += 12;
    PUSH32(g_esp, g_eax);
    PUSH32(g_esp, 0x000DABC0u);
    EU_OPTION_CALL(0x00119020u, sub_00119020, f);
    g_esp += 8;
}

static int pause_screen_size_add(uint32_t screen, GuestFrame f)
{
    /* Reuse the original TV-size slider, without the main-menu calibration
     * decorations or its +0x150 flag. Pause audio uses this same slider ABI. */
    if (MEM32(0x0034F51Cu) != 0x000B4F00u ||
        MEM32(0x0034F524u) != 0x000B4F40u) {
        fprintf(stderr, "[options] EU screen-size row vtable differs\n");
        return 0;
    }
    PUSH32(g_esp, 0x98u);
    PUSH32(g_esp, 0x000CE81Eu);
    EU_OPTION_CALL(0x0017F940u, sub_0017F940, f);
    g_esp += 4;
    uint32_t row = g_eax;
    if (!row) {
        fprintf(stderr, "[options] pause screen-size row allocation failed\n");
        return 0;
    }
    PUSH32(g_esp, 1);
    PUSH32(g_esp, screen + 0x120u);
    PUSH32(g_esp, screen + 0x118u);
    PUSH32(g_esp, screen + 0x110u);
    PUSH32(g_esp, screen + 0x100u);
    g_ecx = row;
    PUSH32(g_esp, 0x000CE856u);
    EU_OPTION_CALL(0x0012BAB0u, sub_0012BAB0, f);
    MEM32(row) = 0x0034F518u;
    PUSH32(g_esp, 1);
    PUSH32(g_esp, row);
    g_ecx = screen;
    PUSH32(g_esp, 0x000CE871u);
    EU_OPTION_CALL(0x0015BBD0u, sub_0015BBD0, f);
    return 1;
}

void sub_000DC4B0(void)
{
    uint32_t screen = g_ecx;
    sub_000DC4B0_gen();
    if (MEM32(0x00407404u) != 1u)
        return;
    GuestFrame f = frame_save();
    pause_caption(screen, 0x0034D46Cu, 0x1D7BB6C9u, f); /* Header_text */
    pause_caption(screen, 0x00350CC8u, 0xE8E24508u, f); /* category title */
    video_notice_begin(screen, 1, f);
    int added = pause_screen_size_add(screen, f);
    added += video_rows_add(screen, f);
    if (added)
        video_notice_update(f);
    PUSH32(g_esp, MEM32(screen + 4));
    PUSH32(g_esp, 0x000DC544u);
    EU_OPTION_CALL(0x00118560u, sub_00118560, f);
    g_esp += 4;
    PUSH32(g_esp, g_eax);
    g_ecx = screen;
    PUSH32(g_esp, 0x000DC54Fu);
    EU_OPTION_CALL(0x00146290u, sub_00146290, f);
    frame_restore(f, 0);
    fprintf(stderr, "[options] pause Video page: %d rows\n", added);
}
static void opt_row_text(uint32_t row, const OptRow *o, GuestFrame f)
{
    PUSH32(g_esp, o->label);
    PUSH32(g_esp, MEM32(row + 0x2Cu));
    PUSH32(g_esp, 0x000B5181u);
    EU_OPTION_CALL(0x00119020u, sub_00119020, f);
    g_esp += 8;
    PUSH32(g_esp, o->texts[*o->value]);
    PUSH32(g_esp, MEM32(row + 0x30u));
    PUSH32(g_esp, 0x000B519Eu);
    EU_OPTION_CALL(0x00119020u, sub_00119020, f);
    g_esp += 8;
}
void sub_000B5170(void)
{
    OptRow *o = opt_row(g_ecx);
    if (!o) { sub_000B5170_gen(); return; }
    RECOMP_EU_ENTER(0x000B5170u);RECOMP_PREEMPT();
    GuestFrame f = frame_save();
    opt_row_text(g_ecx, o, f);
    frame_restore(f, 4);
}
void sub_000B5110(void)
{
    uint32_t row = g_ecx, msg = MEM32(g_esp + 8);
    OptRow *o = opt_row(row);
    if (!o) { sub_000B5110_gen(); return; }
    RECOMP_EU_ENTER(0x000B5110u);RECOMP_PREEMPT();
    GuestFrame f = frame_save();
    if (msg == FE_PAD_LEFT || msg == FE_PAD_RIGHT) {
        *o->value = (*o->value + (msg == FE_PAD_RIGHT ? 1 : o->n - 1)) % o->n;
        o->changed();refl_views();options_save();
        for (unsigned i = 0; i < sizeof s_video_rows / sizeof s_video_rows[0]; ++i)
            if (s_video_rows[i] == row) { video_notice_update(f); break; }
        fprintf(stderr, "[options] car reflections %s, resolution scale %d%%\n",
                s_refl ? "on" : "off", s_scale_pct[s_scale_idx]);
    }
    PUSH32(g_esp, msg);
    g_ecx = row;
    PUSH32(g_esp, 0x000B5141u);
    EU_OPTION_CALL(0x0012B910u, sub_0012B910, f);
    opt_row_text(row, o, f);
    frame_restore(f, 12);
}
void sub_0009B680(void)
{
    sub_0009B680_gen();
    refl_views();
}
