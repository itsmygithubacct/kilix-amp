#include "zip.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define EOCD_SIG  0x06054b50u
#define CDIR_SIG  0x02014b50u
#define LOCAL_SIG 0x04034b50u
#define ZIP_STORED  0
#define ZIP_DEFLATE 8

typedef struct {
    char *name;
    uint32_t csize, usize;
    uint16_t method;
    uint32_t local_off;
    uint32_t external_attr;
} ZipEntry;

struct ZipArchive {
    uint8_t *data;
    size_t size;
    ZipEntry *entries;
    int count;
};

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

ZipArchive *zip_open(const char *path)
{
    size_t size;
    uint8_t *data = (uint8_t *)ka_read_file(path, &size);
    if (!data || size < 22) {
        free(data);
        return NULL;
    }

    /* Find End of Central Directory: scan backwards over trailing comment. */
    size_t scan_start = size >= 22 + 65535 ? size - 22 - 65535 : 0;
    long eocd = -1;
    for (size_t i = size - 22; ; i--) {
        if (rd32(data + i) == EOCD_SIG) {
            eocd = (long)i;
            break;
        }
        if (i == scan_start)
            break;
    }
    if (eocd < 0) {
        free(data);
        return NULL;
    }

    uint16_t total = rd16(data + eocd + 10);
    uint32_t cd_size = rd32(data + eocd + 12);
    uint32_t cd_off = rd32(data + eocd + 16);
    if ((size_t)cd_off + cd_size > size) {
        free(data);
        return NULL;
    }

    ZipArchive *za = calloc(1, sizeof(*za));
    if (!za)
        abort();
    za->data = data;
    za->size = size;
    za->entries = calloc(total ? total : 1, sizeof(ZipEntry));
    if (!za->entries)
        abort();

    const uint8_t *p = data + cd_off;
    const uint8_t *end = data + cd_off + cd_size;
    for (int i = 0; i < total; i++) {
        if (p + 46 > end || rd32(p) != CDIR_SIG)
            break;
        uint16_t namelen = rd16(p + 28);
        uint16_t extralen = rd16(p + 30);
        uint16_t commentlen = rd16(p + 32);
        if (p + 46 + namelen > end)
            break;
        ZipEntry *e = &za->entries[za->count];
        e->method = rd16(p + 10);
        e->csize = rd32(p + 20);
        e->usize = rd32(p + 24);
        e->external_attr = rd32(p + 38);
        e->local_off = rd32(p + 42);
        e->name = ka_strndup((const char *)p + 46, namelen);
        za->count++;
        p += 46 + namelen + extralen + commentlen;
    }
    return za;
}

void zip_close(ZipArchive *za)
{
    if (!za)
        return;
    for (int i = 0; i < za->count; i++)
        free(za->entries[i].name);
    free(za->entries);
    free(za->data);
    free(za);
}

int zip_count(const ZipArchive *za) { return za->count; }

const char *zip_name(const ZipArchive *za, int i)
{
    return za->entries[i].name;
}

uint32_t zip_uncompressed_size(const ZipArchive *za, int i)
{
    return za->entries[i].usize;
}

bool zip_is_symlink(const ZipArchive *za, int i)
{
    /* High 16 bits hold Unix mode; S_IFLNK == 0120000. */
    return ((za->entries[i].external_attr >> 16) & 0170000) == 0120000;
}

bool zip_is_unsafe_name(const ZipArchive *za, int i)
{
    const char *name = za->entries[i].name;
    if (name[0] == '/' || name[0] == '\\')
        return true;
    if (name[0] && name[1] == ':') /* windows drive letter */
        return true;
    /* Reject any ".." path component (either separator). */
    const char *p = name;
    while (*p) {
        const char *comp = p;
        while (*p && *p != '/' && *p != '\\')
            p++;
        if (p - comp == 2 && comp[0] == '.' && comp[1] == '.')
            return true;
        if (*p)
            p++;
    }
    return false;
}

uint8_t *zip_read(const ZipArchive *za, int i, size_t *out_len)
{
    if (i < 0 || i >= za->count)
        return NULL;
    const ZipEntry *e = &za->entries[i];
    if ((size_t)e->local_off + 30 > za->size)
        return NULL;
    const uint8_t *lh = za->data + e->local_off;
    if (rd32(lh) != LOCAL_SIG)
        return NULL;
    uint16_t namelen = rd16(lh + 26);
    uint16_t extralen = rd16(lh + 28);
    size_t data_off = (size_t)e->local_off + 30 + namelen + extralen;
    /* Sizes in the local header may be deferred to a data descriptor;
     * the central directory values (e->csize/usize) are authoritative. */
    if (data_off + e->csize > za->size)
        return NULL;
    const uint8_t *src = za->data + data_off;

    uint8_t *out = malloc((size_t)e->usize + 1);
    if (!out)
        abort();

    if (e->method == ZIP_STORED) {
        if (e->csize != e->usize) {
            free(out);
            return NULL;
        }
        memcpy(out, src, e->usize);
    } else if (e->method == ZIP_DEFLATE) {
        z_stream zs = {0};
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
            free(out);
            return NULL;
        }
        zs.next_in = (Bytef *)src;
        zs.avail_in = e->csize;
        zs.next_out = out;
        zs.avail_out = e->usize;
        int rc = inflate(&zs, Z_FINISH);
        inflateEnd(&zs);
        if (rc != Z_STREAM_END || zs.total_out != e->usize) {
            free(out);
            return NULL;
        }
    } else {
        free(out);
        return NULL;
    }

    out[e->usize] = '\0';
    if (out_len)
        *out_len = e->usize;
    return out;
}
