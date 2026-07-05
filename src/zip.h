/* Minimal read-only ZIP archive parser (stored + deflate via zlib).
 * Replaces Python's zipfile for .wsz skin loading; entries are read into
 * memory, never extracted to disk. */
#ifndef KA_ZIP_H
#define KA_ZIP_H

#include "common.h"

typedef struct ZipArchive ZipArchive;

ZipArchive *zip_open(const char *path); /* NULL on parse error */
void zip_close(ZipArchive *za);

int zip_count(const ZipArchive *za);
const char *zip_name(const ZipArchive *za, int i);
uint32_t zip_uncompressed_size(const ZipArchive *za, int i);
/* Unix symlink member (external attr high bits). */
bool zip_is_symlink(const ZipArchive *za, int i);
/* True if the stored name is absolute or contains a ".." component. */
bool zip_is_unsafe_name(const ZipArchive *za, int i);
/* Decompressed contents; heap buffer with trailing NUL, caller frees.
 * NULL on corrupt entry or unsupported compression method. */
uint8_t *zip_read(const ZipArchive *za, int i, size_t *out_len);

#endif
