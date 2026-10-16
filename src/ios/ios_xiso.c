/* XDVDFS layout follows xboxrecomp/tools/xiso/xdvdfs.py. Never load an ISO
 * into memory. Reject malformed paths/extents before writing imported data. */
#include "ios_xiso.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define SECTOR 2048u
#define MAGIC "MICROSOFT*XBOX*MEDIA"
static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int fail(char *error, size_t capacity, const char *message)
{
    if (capacity) snprintf(error, capacity, "%s", message);
    return 0;
}
int nfs_iso_read(const NfsIso *iso, uint64_t offset, void *data, size_t size)
{
    unsigned char *p = data;
    if (offset > iso->size || size > iso->size - offset) return 0;
    while (size) {
        ssize_t n = pread(iso->fd, p, size, (off_t)offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
        p += n; offset += (uint64_t)n; size -= (size_t)n;
    }
    return 1;
}
int nfs_iso_open(NfsIso *iso, const char *path, char *error, size_t capacity)
{
    static const uint64_t bases[] = { 0, 0xFD90, 0x30600, 0xFD90000, 0x18300000 };
    struct stat st;
    unsigned char desc[SECTOR];
    memset(iso, 0, sizeof *iso);
    iso->fd = open(path, O_RDONLY);
    if (iso->fd < 0) return fail(error, capacity, "Cannot open the ISO. Download it from iCloud first.");
    if (fstat(iso->fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) {
        nfs_iso_close(iso);
        return fail(error, capacity, "Cannot read the ISO file.");
    }
    iso->size = (uint64_t)st.st_size;
    for (size_t i = 0; i < sizeof bases / sizeof bases[0]; i++) {
        if (!nfs_iso_read(iso, bases[i] + 32u * SECTOR, desc, sizeof desc) ||
            memcmp(desc, MAGIC, sizeof MAGIC - 1)) continue;
        iso->base = bases[i];
        iso->root_sector = le32(desc + 0x14);
        iso->root_size = le32(desc + 0x18);
        if (iso->root_size) return 1;
        break;
    }
    nfs_iso_close(iso);
    return fail(error, capacity, "Not a supported Xbox XDVDFS ISO (or its directory is empty).");
}
void nfs_iso_close(NfsIso *iso)
{
    if (iso->fd >= 0) close(iso->fd);
    iso->fd = -1;
}
typedef struct {
    const NfsIso *iso;
    NfsIsoVisitor visit;
    void *context;
    char *error;
    size_t capacity;
    uint32_t ancestors[32];
    unsigned entries;
    uint64_t directory_bytes;
} Walk;
static int directory(Walk *w, uint32_t sector, uint32_t size, const char *parent, unsigned depth)
{
    unsigned char data[SECTOR];
    uint64_t offset = w->iso->base + (uint64_t)sector * SECTOR;
    if (depth >= 32 || size > (16u << 20) || (w->directory_bytes += size) > (32u << 20) ||
        offset > w->iso->size || size > w->iso->size - offset)
        return fail(w->error, w->capacity, "ISO directory is too large, too deep or outside the image.");
    for (unsigned i = 0; i < depth; i++)
        if (w->ancestors[i] == sector) return fail(w->error, w->capacity, "ISO contains a directory cycle.");
    w->ancestors[depth] = sector;
    for (uint32_t at = 0; at < size; at += SECTOR) {
        size_t length = size - at < SECTOR ? size - at : SECTOR;
        if (!nfs_iso_read(w->iso, offset + at, data, length))
            return fail(w->error, w->capacity, "ISO is truncated or unreadable.");
        for (size_t pos = 0; pos + 14 <= length;) {
            const unsigned char *e = data + pos;
            unsigned n = e[13];
            char name[256], path[1024];
            uint32_t sec = le32(e + 4), bytes = le32(e + 8);
            int dir = (e[12] & 0x10) != 0;
            uint64_t file_at = w->iso->base + (uint64_t)sec * SECTOR;
            if (!n || (e[0] == 255 && e[1] == 255 && e[2] == 255 && e[3] == 255)) break;
            if (pos + 14 + n > length || ++w->entries > 100000)
                return fail(w->error, w->capacity, "Malformed ISO directory entry.");
            for (unsigned i = 0; i < n; i++) {
                unsigned c = e[14 + i];
                if (c < 32 || c > 126 || c == '/' || c == '\\' || c == ':')
                    return fail(w->error, w->capacity, "ISO contains an unsafe filename.");
                name[i] = (char)c;
            }
            name[n] = 0;
            if (!strcmp(name, ".") || !strcmp(name, ".."))
                return fail(w->error, w->capacity, "ISO contains an unsafe filename.");
            int count = snprintf(path, sizeof path, "%s%s%s", parent, *parent ? "/" : "", name);
            if (count < 0 || (size_t)count >= sizeof path || file_at > w->iso->size || bytes > w->iso->size - file_at)
                return fail(w->error, w->capacity, "ISO file path or extent is invalid.");
            if (!w->visit(w->context, path, file_at, bytes, dir))
                return fail(w->error, w->capacity, "Import stopped while processing an ISO entry.");
            if (dir && bytes && !directory(w, sec, bytes, path, depth + 1)) return 0;
            pos += (14 + n + 3) & ~(size_t)3;
        }
    }
    return 1;
}
int nfs_iso_walk(const NfsIso *iso, NfsIsoVisitor visit, void *context, char *error, size_t capacity)
{
    Walk w = { .iso = iso, .visit = visit, .context = context, .error = error, .capacity = capacity };
    return directory(&w, iso->root_sector, iso->root_size, "", 0);
}
