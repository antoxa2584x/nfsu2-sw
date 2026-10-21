/* Exact draw-data reuse, valid only in the current command-buffer generation.
 * Sampled hashes find candidates; full source-byte equality decides every hit.
 * CPU snapshots are bounded and ordinary cached memory. Guest pointers, epochs
 * and constant generation numbers alone never establish immutability. */
#ifndef NFSU2_VK_DATA_CACHE_H
#define NFSU2_VK_DATA_CACHE_H

#define DATA_CACHE_SLOTS 64u
#define DATA_INDEX_MAX 1024u
typedef struct {
    uint32_t generation, hash, used;
    VkDeviceSize offset;
    uint8_t value[VK_CONST_SIZE];
} DataConstant;
typedef struct {
    uint32_t generation, hash, count, type, used;
    VkDeviceSize offset;
    uint32_t value[DATA_INDEX_MAX];
} DataIndex;
static DataConstant s_data_constants[DATA_CACHE_SLOTS];
static DataIndex s_data_indices[DATA_CACHE_SLOTS];
static uint32_t s_data_const_hit, s_data_const_miss, s_data_const_bypass;
static uint32_t s_data_index_hit, s_data_index_miss;
static uint32_t s_data_bind_sent, s_data_bind_saved;
static uint64_t s_data_const_saved, s_data_index_saved;
static uint32_t s_data_serial;

static int data_cache_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *e = getenv("RECOMP_VK_DATA_CACHE");
        enabled = !(e && *e == '0');
        LOGE("exact draw data cache %s (command generation scoped)\n", enabled ? "on" : "off");
    }
    return enabled;
}

static uint32_t data_hash(const void *value, uint32_t bytes, uint32_t kind)
{
    uint32_t h = 2166136261u ^ bytes ^ kind;
    const uint8_t *p = (const uint8_t *)value;
    /* Both callers pass at least four bytes. Avoid hashing every 3-KiB block;
     * changed unsampled bytes are still caught by the exact memcmp below. */
    for (uint32_t i = 0; i < 16; ++i) {
        uint32_t word, at = (((bytes - 4) * i) / 15) & ~3u;
        memcpy(&word, p + at, sizeof word);
        h = (h ^ word) * 16777619u;
    }
    return h;
}

static int constant_upload(const void *value, VkDeviceSize *offset)
{
    DataConstant *entry = NULL;
    void *mapped;
    if (data_cache_enabled()) {
        uint32_t h = data_hash(value, VK_CONST_SIZE, 0x434f4e53u);
        uint32_t bucket = (h & (DATA_CACHE_SLOTS / 2 - 1)) * 2;
        DataConstant *victim = NULL;
        for (uint32_t way = 0; way < 2; ++way) {
            entry = &s_data_constants[bucket + way];
            if (entry->generation == s_cb_gen && entry->hash == h
                && !(entry->offset % s_ubo_align)
                && !memcmp(entry->value, value, VK_CONST_SIZE)) {
                *offset = entry->offset;
                entry->used = ++s_data_serial;
                ++s_data_const_hit;
                s_data_const_saved += VK_CONST_SIZE;
                return 1;
            }
            if (!victim || (victim->generation == s_cb_gen &&
                (entry->generation != s_cb_gen || entry->used < victim->used))) victim = entry;
        }
        entry = victim;
        entry->used = ++s_data_serial;
        entry->hash = h;
    }
    *offset = ring_alloc(VK_CONST_SIZE, s_ubo_align, &mapped);
    if (!mapped) return 0;
    memcpy(mapped, value, VK_CONST_SIZE);
    if (entry) {
        memcpy(entry->value, value, VK_CONST_SIZE);
        entry->offset = *offset;
        entry->generation = s_cb_gen; /* Allocation may have flushed the ring. */
    }
    ++s_data_const_miss;
    return 1;
}

static int index_upload(const uint32_t *value, uint32_t count,
                        VkIndexType type, VkDeviceSize *offset)
{
    DataIndex *entry = NULL;
    void *mapped;
    VkDeviceSize bytes = (VkDeviceSize)count * (type == VK_INDEX_TYPE_UINT16 ? 2 : 4);
    if (count && count <= DATA_INDEX_MAX && data_cache_enabled()) {
        uint32_t h = data_hash(value, count * 4, (uint32_t)type ^ 0x494e4458u);
        uint32_t bucket = (h & (DATA_CACHE_SLOTS / 2 - 1)) * 2;
        DataIndex *victim = NULL;
        for (uint32_t way = 0; way < 2; ++way) {
            entry = &s_data_indices[bucket + way];
            if (entry->generation == s_cb_gen && entry->hash == h
                && entry->count == count && entry->type == (uint32_t)type
                && !memcmp(entry->value, value, count * 4)) {
                *offset = entry->offset;
                entry->used = ++s_data_serial;
                ++s_data_index_hit;
                s_data_index_saved += bytes;
                return 1;
            }
            if (!victim || (victim->generation == s_cb_gen &&
                (entry->generation != s_cb_gen || entry->used < victim->used))) victim = entry;
        }
        entry = victim;
        entry->used = ++s_data_serial;
        entry->hash = h;
    }
    *offset = ring_alloc(bytes, 4, &mapped);
    if (!mapped) return 0;
    if (type == VK_INDEX_TYPE_UINT16) {
        uint16_t *out = (uint16_t *)mapped;
        for (uint32_t i = 0; i < count; ++i) out[i] = (uint16_t)value[i];
    } else memcpy(mapped, value, (size_t)bytes);
    if (entry) {
        memcpy(entry->value, value, count * 4);
        entry->offset = *offset;
        entry->count = count;
        entry->type = (uint32_t)type;
        entry->generation = s_cb_gen;
    }
    ++s_data_index_miss;
    return 1;
}

static void bind_draw_indices(VkDeviceSize offset, VkIndexType type)
{
    static uint32_t generation;
    static VkBuffer buffer;
    static VkDeviceSize last_offset;
    static VkIndexType last_type;
    if (data_cache_enabled() && generation == s_cb_gen && buffer == s_f->ring
        && last_offset == offset && last_type == type) {
        ++s_data_bind_saved;
        return;
    }
    vkCmdBindIndexBuffer(s_cb, s_f->ring, offset, type);
    generation = s_cb_gen;
    buffer = s_f->ring;
    last_offset = offset;
    last_type = type;
    ++s_data_bind_sent;
}

static void data_cache_report(uint32_t frames)
{
    if (!frames) return;
    LOGE("data cache per frame: constants hit/miss/bypass %.1f/%.1f/%.1f, %.1f KiB saved; "
         "indices hit/miss %.1f/%.1f, %.1f KiB saved; index binds sent/saved %.1f/%.1f\n",
         (double)s_data_const_hit / frames, (double)s_data_const_miss / frames,
         (double)s_data_const_bypass / frames, s_data_const_saved / 1024.0 / frames,
         (double)s_data_index_hit / frames, (double)s_data_index_miss / frames,
         s_data_index_saved / 1024.0 / frames,
         (double)s_data_bind_sent / frames, (double)s_data_bind_saved / frames);
    s_data_const_hit = s_data_const_miss = s_data_const_bypass = 0;
    s_data_index_hit = s_data_index_miss = 0;
    s_data_bind_sent = s_data_bind_saved = 0;
    s_data_const_saved = s_data_index_saved = 0;
}
#endif
