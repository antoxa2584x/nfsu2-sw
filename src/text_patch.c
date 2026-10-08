/*
 * Switch wording for the game's English text.
 *
 * NFSU2 keeps its text in a language chunk (id 0x00039000) inside the
 * ZZDATA pack files: header {0x10, count, table offset, pool offset}, then
 * a table of {label hash, pool offset} sorted by hash and a packed pool of
 * NUL-terminated strings. Button *icons* ($JOY_EVENT_...$ tokens) are
 * textures; this only changes words: "START" -> "+", the replay screen's
 * "Black" -> "R" (xinput_nx.c: Start = +, Black = R), and Xbox names
 * (Xbox Live, hard disk, Dashboard, Gamertag, Thumbstick) -> neutral ones.
 *
 * The chunk is patched as NtReadFile delivers it (xbox_file_read_hook):
 * every string is rewritten into a new pool, which must not grow, and the
 * table offsets follow. The Switch wording goes into the English table
 * only (recognised by one known string); NFSU2_SWITCH_TEXT=0 turns it off,
 * =1 turns it on outside the Switch build. s_always goes into every
 * language, on every build: the texts of the port's own options
 * (recomp_manual.c), which take over PC-only strings the Xbox never shows.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* kernel_file.c */
extern void (*xbox_file_read_hook)(void *buf, size_t len, int64_t offset);

#define LANG_CHUNK_ID   0x00039000u
#define LANG_ALIGN      0x800u      /* chunks start on pack sectors */
#define HASH_ENGLISH    0x45A6A9CEu /* TITLE_SCREEN_START_PROMPT_XBOX */
#define TEXT_ENGLISH    "Please press START to begin"

/* Whole strings, by label hash, on every build. The pool must not grow,
 * so these are no longer than what they replace. */
static const struct { uint32_t hash; const char *text; } s_always[] = {
    /* Options -> Video rows */
    { 0x8FE9288Eu, "Car Reflections" },          /* "Car Reflection Detail" */
    { 0x4AC50BCFu, "Resolution Scale" },         /* "Car Geometry Detail" */
    { 0x2A2A4AB3u, "1x" },                       /* "Road Reflection Detail" */
    { 0x822E4E9Bu, "1.5x" },                     /* "Motion Blur" */
    { 0xB55E7665u, "2x" },                       /* "Level Of Detail" */
    { 0xD3588630u, "2.5x" },                     /* "Widescreen" */
};

static int s_switch_words;

/* Whole strings, by label hash. */
static const struct { uint32_t hash; const char *text; } s_by_hash[] = {
    { 0x45A6A9CEu, "Please press + to begin" },  /* TITLE_SCREEN_START_PROMPT_XBOX */
    { 0xEEEB248Eu, "Press +" },                  /* TITLE_SCREEN_START_PROMPT */
    { 0x1F181F57u, "Press +" },                  /* TITLE_SCREEN_START_PROMPT_GC */
    { 0xFC6731A6u, "Player One Press +" },       /* PLAYER_ONE_PRESS_START */
    { 0x5BC4BC1Eu, "Player Two Press +" },       /* PLAYER_TWO_PRESS_START */
    { 0x5A890BDAu, "Player One Press +" },       /* PLAYER_ONE_PRESS_START_PS2 */
    { 0xEA171252u, "Player Two Press +" },       /* PLAYER_TWO_PRESS_START_PS2 */
    { 0xD461C58Au, "+" },                        /* UI_START (replay legend) */
    { 0xC5146FE8u, "R" },                        /* UI_REPLAY_SELECT_XBOX "Black" */
    { 0x3991DD49u, "SD Card" },                  /* IO_DEVICE_XBOX_DESCRIPTION */
    { 0xF46A8B02u, "Headset" },                  /* OLX_XBOX_COMMUNICATOR */
    { 0x1B99CA46u, "Your console cannot connect to the online service."
                   "  Do you want to start the troubleshooter?" },
    { 0xFD9E06CDu, "Are you sure you want to exit to the HOME Menu"
                   " and create a new online account?" },
};

/* Substrings, first match wins at each position; for every other string. */
static const struct { const char *from, *to; } s_rules[] = {
    { "EA Messenger and Xbox Live Friends List", "EA Messenger Friends List" },
    { "Xbox Live OptiMatch ", "OptiMatch " },
    { "Signing in to Xbox Live", "Signing in online" },
    { "Sign in to Xbox Live", "Sign In Online" },
    { "sign out of Xbox Live", "sign out" },
    { "Sign out of Xbox Live", "Sign out" },
    { "signed out of Xbox Live", "signed out" },
    { "connection to Xbox Live", "connection to the online service" },
    { "from Xbox Live", "from the online service" },
    { "the Xbox Live service", "the online service" },
    { "The Xbox Live service", "The online service" },
    { "Xbox Live message", "online message" },
    { "to Xbox Live until", "online until" },
    { "Xbox Live", "Online" },
    { "your Xbox personal information", "your personal information" },
    { "Xbox hard disk", "SD card" },
    { "Xbox Dashboard", "HOME Menu" },
    { "Gamertag", "Username" },
    { "Thumbstick", "Stick" },
};

/* src -> dst (cap bytes), NUL-terminated; returns the length or -1. */
static int rewrite(uint32_t hash, const char *src, char *dst, size_t cap, int english)
{
    size_t n = 0, i;
    for (i = 0; i < sizeof s_always / sizeof s_always[0]; i++)
        if (s_always[i].hash == hash) { src = s_always[i].text; goto copy; }
    if (!s_switch_words || !english)
        goto copy;
    for (i = 0; i < sizeof s_by_hash / sizeof s_by_hash[0]; i++)
        if (s_by_hash[i].hash == hash) { src = s_by_hash[i].text; goto copy; }
    while (*src) {
        for (i = 0; i < sizeof s_rules / sizeof s_rules[0]; i++) {
            size_t fl = strlen(s_rules[i].from), tl = strlen(s_rules[i].to);
            if (strncmp(src, s_rules[i].from, fl) == 0) {
                if (n + tl >= cap) return -1;
                memcpy(dst + n, s_rules[i].to, tl);
                n += tl; src += fl;
                break;
            }
        }
        if (i == sizeof s_rules / sizeof s_rules[0]) {
            if (n + 1 >= cap) return -1;
            dst[n++] = *src++;
        }
    }
    dst[n] = 0;
    return (int)n;
copy:
    n = strlen(src);
    if (n >= cap) return -1;
    memcpy(dst, src, n + 1);
    return (int)n;
}

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static void wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

/* One language chunk at c, avail bytes of it in the buffer. */
static void patch_chunk(uint8_t *c, size_t avail)
{
    uint32_t size = rd32(c + 4);
    uint8_t *base = c + 8;
    if (rd32(base) != 0x10) return;
    uint32_t count = rd32(base + 4), tab = rd32(base + 8), pool = rd32(base + 12);
    if (count == 0 || count > 65536 || tab < 0x10 || pool < tab ||
        (uint64_t)tab + 8ull * count > pool || pool >= size)
        return;
    if (avail < 8 + (size_t)size) {
        fprintf(stderr, "[TEXT] language chunk split across reads, not patched\n");
        return;
    }
    uint8_t *t = base + tab;
    char *p = (char *)base + pool;
    size_t plen = size - pool;
    uint32_t k;
    int english = 0;

    for (k = 0; k < count; k++)
        if (rd32(t + 8 * k) == HASH_ENGLISH) {
            uint32_t o = rd32(t + 8 * k + 4);
            english = o < plen && strncmp(p + o, TEXT_ENGLISH, plen - o) == 0;
            break;
        }

    char *out = malloc(plen);
    uint32_t *noff = malloc(count * sizeof *noff);
    if (!out || !noff) { free(out); free(noff); return; }
    size_t used = 0;
    int changed = 0;
    for (k = 0; k < count; k++) {
        uint32_t o = rd32(t + 8 * k + 4);
        if (o >= plen || !memchr(p + o, 0, plen - o)) goto fail;
        int n = rewrite(rd32(t + 8 * k), p + o, out + used, plen - used, english);
        if (n < 0) goto fail;
        if (strcmp(out + used, p + o) != 0) changed++;
        noff[k] = (uint32_t)used;
        used += (size_t)n + 1;
    }
    memcpy(p, out, used);
    memset(p + used, 0, plen - used);
    for (k = 0; k < count; k++)
        wr32(t + 8 * k + 4, noff[k]);
    printf("[TEXT] %s: %d of %u strings changed\n",
           english && s_switch_words ? "Switch wording" : "option labels", changed, count);
    free(out); free(noff);
    return;
fail:
    fprintf(stderr, "[TEXT] language chunk not patched (pool overflow or bad offset)\n");
    free(out); free(noff);
}

static void on_read(void *buf, size_t len, int64_t offset)
{
    uint8_t *b = buf;
    size_t i = 0;
    if (len < 32) return;
    if (offset >= 0 && offset % LANG_ALIGN)
        i = LANG_ALIGN - (size_t)(offset % LANG_ALIGN);
    for (; i + 32 <= len; i += LANG_ALIGN)
        if (rd32(b + i) == LANG_CHUNK_ID)
            patch_chunk(b + i, len - i);
}

void nfsu2_text_patch_init(void)
{
    const char *e = getenv("NFSU2_SWITCH_TEXT");
#ifdef __SWITCH__
    int on = !(e && e[0] == '0');
#else
    int on = e && e[0] == '1';
#endif
    s_switch_words = on;
    xbox_file_read_hook = on_read;
}
