/* Shared renderer caches: exact bytes, same command generation only.
 * No guest data or image contents are considered immutable across frames.
 * Every byte in a uniform key participates, including independent Y snapping. */
#ifndef NFSU2_VK_STATE_CACHE_H
#define NFSU2_VK_STATE_CACHE_H

#define STATE_UNIFORM_BYTES 512
#define STATE_UNIFORM_SLOTS 64
typedef struct {
    uint32_t generation, hash, bytes;
    VkDeviceSize offset;
    uint8_t value[STATE_UNIFORM_BYTES];
} StateUniform;
static StateUniform s_uniform_cache[STATE_UNIFORM_SLOTS];
static uint32_t s_cache_uniform_hit, s_cache_uniform_miss;
static uint32_t s_cache_desc_sent, s_cache_desc_saved;
static uint64_t s_cache_uniform_bytes;

static int state_cache_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *e = getenv("RECOMP_VK_STATE_CACHE");
        enabled = !(e && *e == '0');
        LOGE("exact draw state cache %s (command generation scoped)\n",
             enabled ? "on" : "off");
    }
    return enabled;
}

static int uniform_upload(const void *value, uint32_t bytes, VkDeviceSize *offset)
{
    StateUniform *entry = NULL;
    void *mapped;
    if (state_cache_enabled() && bytes <= STATE_UNIFORM_BYTES) {
        uint32_t h = 2166136261u ^ bytes;
        const uint8_t *data = value;
        for (uint32_t i = 0; i < bytes; i += 4) {
            uint32_t word = 0;
            uint32_t n = bytes - i < 4 ? bytes - i : 4;
            memcpy(&word, data + i, n); /* No float/int aliasing. */
            h = (h ^ word) * 16777619u;
        }
        entry = &s_uniform_cache[h & (STATE_UNIFORM_SLOTS - 1)];
        if (entry->generation == s_cb_gen && entry->hash == h
            && entry->bytes == bytes && !memcmp(entry->value, value, bytes)) {
            *offset = entry->offset;
            ++s_cache_uniform_hit;
            s_cache_uniform_bytes += bytes;
            return 1;
        }
        entry->hash = h;
    }
    *offset = ring_alloc(bytes, s_ubo_align, &mapped);
    if (!mapped) return 0;
    memcpy(mapped, value, bytes);
    if (entry) {
        entry->offset = *offset;
        entry->bytes = bytes;
        /* ring_alloc may have flushed; use the resulting generation. */
        entry->generation = s_cb_gen;
        memcpy(entry->value, value, bytes);
    }
    ++s_cache_uniform_miss;
    return 1;
}

static uint32_t s_descriptor_generation, s_descriptor_valid;
/* An incompatible post-process layout disturbs push descriptors. */
static void draw_descriptors_invalidate(void)
{
    s_descriptor_valid = 0;
}
static void push_draw_descriptors(const VkWriteDescriptorSet writes[7])
{
    static VkDescriptorBufferInfo buffers[7];
    static VkDescriptorImageInfo images[7];
    VkWriteDescriptorSet changed[7];
    uint32_t n = 0;
    if (!state_cache_enabled()) {
        p_push_desc(s_cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s_layout, 0, 7, writes);
        s_cache_desc_sent += 7;
        return;
    }
    if (s_descriptor_generation != s_cb_gen) { s_descriptor_generation = s_cb_gen; s_descriptor_valid = 0; }
    /* Only guest draws use s_layout; post-processing invalidates this cache. */
    for (uint32_t i = 0; i < 7; ++i) {
        const VkWriteDescriptorSet *w = &writes[i];
        uint32_t binding = w->dstBinding;
        int same = (s_descriptor_valid & (1u << binding)) != 0;
        if (w->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            const VkDescriptorBufferInfo *b = w->pBufferInfo;
            const VkDescriptorBufferInfo *old = &buffers[binding];
            same = same && old->buffer == b->buffer && old->offset == b->offset && old->range == b->range;
            buffers[binding] = *b;
        } else {
            const VkDescriptorImageInfo *b = w->pImageInfo;
            const VkDescriptorImageInfo *old = &images[binding];
            same = same && old->sampler == b->sampler && old->imageView == b->imageView
                        && old->imageLayout == b->imageLayout;
            images[binding] = *b;
        }
        s_descriptor_valid |= 1u << binding;
        if (!same) changed[n++] = *w;
    }
    if (n) p_push_desc(s_cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s_layout, 0, n, changed);
    s_cache_desc_sent += n;
    s_cache_desc_saved += 7 - n;
}

static void state_cache_report(uint32_t frames)
{
    if (!frames) return;
    LOGE("draw cache per frame: uniform hit/miss %.1f/%.1f, %.1f KiB saved; "
         "descriptor writes sent/saved %.1f/%.1f\n",
         (double)s_cache_uniform_hit / frames, (double)s_cache_uniform_miss / frames,
         s_cache_uniform_bytes / 1024.0 / frames,
         (double)s_cache_desc_sent / frames, (double)s_cache_desc_saved / frames);
    s_cache_uniform_hit = s_cache_uniform_miss = 0;
    s_cache_desc_sent = s_cache_desc_saved = 0;
    s_cache_uniform_bytes = 0;
}
#endif
