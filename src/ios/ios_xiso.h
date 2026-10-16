/* Streaming Xbox XDVDFS reader, independent of UIKit and executable region. */
#ifndef NFSU2_IOS_XISO_H
#define NFSU2_IOS_XISO_H
#include <stddef.h>
#include <stdint.h>
typedef struct { int fd; uint64_t size, base; uint32_t root_sector, root_size; } NfsIso;
typedef int (*NfsIsoVisitor)(void *context, const char *path, uint64_t offset,
                            uint32_t size, int directory);
int nfs_iso_open(NfsIso *iso, const char *path, char *error, size_t capacity);
void nfs_iso_close(NfsIso *iso);
int nfs_iso_read(const NfsIso *iso, uint64_t offset, void *data, size_t size);
int nfs_iso_walk(const NfsIso *iso, NfsIsoVisitor visit, void *context,
                  char *error, size_t capacity);
#endif
