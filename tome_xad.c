/* tome_xad.c - XAD Client for Apple installer "tome" archives.
 *
 * ANSI C (C89) implementation of an XAD client library that consumes
 * Apple installer "tome" archives, including the InstaCompOne
 * decompression codec used by the Apple installer SDK, plus the
 * MacBinary II/III fork format used to wrap the actual payloads.
 *
 * Reference implementations this code is ported from:
 *   - kainjow/TomeViewerX          (MIT)   Tome container metadata
 *   - maximumspatium/ResDecompress / elliotnunn/macresources  (MIT)  InstaCompOne
 *   - billcavalieri/tome-extract  (ported from the above)        fork handling
 */

#include "xadClient.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* Tome container format constants                                     */
/* ------------------------------------------------------------------ */

#define TOME_MAGIC        0x6B630001L
#define TOME_HEADER_SIZE  36
#define TOME_SECTION_SIZE 128
#define TOME_CHRP_PREFIX  "<CHRP-BOOT>"
#define TOME_CHRP_PREFIX_LEN  10
#define TOME_RECOG_SIZE   36

#define TOME_CLIENT_VERSION   1
#define TOME_CLIENT_REVISION  0
#define TOME_ARCHIVERNAME     "tome (Apple installer)"

/* ------------------------------------------------------------------ */
/* InstaCompOne codec constants                                        */
/* ------------------------------------------------------------------ */

#define XAD_INSTA_COMP_MAGIC     0xA89F6572
#define XAD_INSTA_COMP_HEADER_LEN  18
#define XAD_INSTA_COMP_VERSION   9
#define XAD_INSTA_COMP_FLAG      1
#define XAD_INSTA_COMP_CODEC_ID  3
#define XAD_INSTA_COMP_SKIP_BYTES  4
#define XAD_LIT_MAX_LEN 63

static const char xad_tome_rom_name[] = "Mac OS ROM";
static const char xad_tome_chrp_prefix[] = "<CHRP-BOOT>";
static const unsigned char xad_tome_stored_prefix[4] = {1, 0x94, 0xFF, 0xFF};

/* ------------------------------------------------------------------ */
/* Internal data structures                                            */
/* ------------------------------------------------------------------ */

typedef struct XADTomeFile {
    xadUINT16 t_id;
    char t_name[32];
    xadUINT32 t_type;
    xadUINT32 t_creator;
    xadUINT32 t_version;
    xadUINT32 t_data_size;
    xadUINT32 t_data_offset;
    xadUINT32 t_data_compressed_size;
    xadUINT32 t_resource_size;
    xadUINT32 t_resource_offset;
    xadUINT32 t_resource_compressed_size;
} XADTomeFile;

typedef struct XADTomeArchive {
    XADTomeFile *files;
    int file_count;
    unsigned char *data;
    unsigned int data_size;
} XADTomeArchive;

typedef struct XADMacBinaryFile {
    char file_name[64];
    char file_type[8];
    char creator[8];
    unsigned char *data_fork;
    unsigned int data_fork_size;
    unsigned char *resource_fork;
    unsigned int resource_fork_size;
} XADMacBinaryFile;

typedef struct TomePrivate {
    XADTomeArchive archive;
    XADTomeFile *file;
    char outDir[512];
} TomePrivate;

/* ------------------------------------------------------------------ */
/* Big-endian readers and MacRoman conversion                        */
/* ------------------------------------------------------------------ */

unsigned int xad_be16(const unsigned char *p, unsigned int offset) {
    return ((unsigned int)p[offset] << 8) | (unsigned int)p[offset + 1];
}

unsigned int xad_be32(const unsigned char *p, unsigned int offset) {
    return ((unsigned int)p[offset] << 24) | ((unsigned int)p[offset + 1] << 16) | ((unsigned int)p[offset + 2] << 8) | (unsigned int)p[offset + 3];
}

void xad_mac_roman(const unsigned char *p, unsigned int len, char *out) {
    unsigned int i;
    for(i = 0; i < len; i++) { out[i] = (char)p[i]; }
    out[len] = 0;
}

/* ------------------------------------------------------------------ */
/* InstaCompOne: Huffman coding                                        */
/* ------------------------------------------------------------------ */

typedef struct XAHuffEntry {
    unsigned int code;
    unsigned int width;
    int is_compact;
    unsigned int extra_bits;
    unsigned int extra_start;
    unsigned int value;
} XAHuffEntry;

typedef struct XAHuffTable {
    XAHuffEntry entries[24];
    int count;
} XAHuffTable;

static XAHuffTable xad_len_huff_table;
static XAHuffTable xad_lit_huff_table;

static void xad_huff_add(XAHuffTable *table, unsigned int code, unsigned int width, int is_compact, unsigned int extra_bits, unsigned int extra_start, unsigned int value) {
    table->entries[table->count].code = code;
    table->entries[table->count].width = width;
    table->entries[table->count].is_compact = is_compact;
    table->entries[table->count].extra_bits = extra_bits;
    table->entries[table->count].extra_start = extra_start;
    table->entries[table->count].value = value;
    table->count = table->count + 1;
}

static void xad_huff_build(void) {
    xad_len_huff_table.count = 0;
    xad_huff_add(&xad_len_huff_table, 0, 2, 0, 0, 0, 0);
    xad_huff_add(&xad_len_huff_table, 1, 2, 0, 0, 0, 1);
    xad_huff_add(&xad_len_huff_table, 4, 3, 0, 0, 0, 2);
    xad_huff_add(&xad_len_huff_table, 10, 4, 0, 0, 0, 3);
    xad_huff_add(&xad_len_huff_table, 11, 4, 0, 0, 0, 4);
    xad_huff_add(&xad_len_huff_table, 24, 5, 0, 0, 0, 5);
    xad_huff_add(&xad_len_huff_table, 25, 5, 0, 0, 0, 6);
    xad_huff_add(&xad_len_huff_table, 52, 7, 0, 0, 0, 7);
    xad_huff_add(&xad_len_huff_table, 53, 7, 0, 0, 0, 8);
    xad_huff_add(&xad_len_huff_table, 54, 7, 0, 0, 0, 9);
    xad_huff_add(&xad_len_huff_table, 55, 7, 0, 0, 0, 10);
    xad_huff_add(&xad_len_huff_table, 14, 4, 1, 3, 11, 0);
    xad_huff_add(&xad_len_huff_table, 30, 5, 1, 3, 19, 0);
    xad_huff_add(&xad_len_huff_table, 62, 6, 1, 5, 27, 0);
    xad_huff_add(&xad_len_huff_table, 126, 7, 1, 6, 59, 0);
    xad_huff_add(&xad_len_huff_table, 254, 8, 1, 7, 123, 0);
    xad_huff_add(&xad_len_huff_table, 510, 9, 1, 8, 251, 0);
    xad_huff_add(&xad_len_huff_table, 1022, 10, 1, 9, 507, 0);
    xad_huff_add(&xad_len_huff_table, 2046, 11, 1, 10, 1019, 0);

    xad_lit_huff_table.count = 0;
    xad_huff_add(&xad_lit_huff_table, 0, 1, 0, 0, 0, 1);
    xad_huff_add(&xad_lit_huff_table, 4, 3, 0, 0, 0, 2);
    xad_huff_add(&xad_lit_huff_table, 5, 3, 0, 0, 0, 3);
    xad_huff_add(&xad_lit_huff_table, 24, 5, 0, 0, 0, 4);
    xad_huff_add(&xad_lit_huff_table, 25, 5, 0, 0, 0, 5);
    xad_huff_add(&xad_lit_huff_table, 26, 5, 0, 0, 0, 6);
    xad_huff_add(&xad_lit_huff_table, 27, 5, 0, 0, 0, 7);
    xad_huff_add(&xad_lit_huff_table, 112, 7, 0, 0, 0, 8);
    xad_huff_add(&xad_lit_huff_table, 113, 7, 0, 0, 0, 9);
    xad_huff_add(&xad_lit_huff_table, 114, 7, 0, 0, 0, 10);
    xad_huff_add(&xad_lit_huff_table, 115, 7, 0, 0, 0, 11);
    xad_huff_add(&xad_lit_huff_table, 116, 7, 0, 0, 0, 12);
    xad_huff_add(&xad_lit_huff_table, 117, 7, 0, 0, 0, 13);
    xad_huff_add(&xad_lit_huff_table, 118, 7, 0, 0, 0, 14);
    xad_huff_add(&xad_lit_huff_table, 119, 7, 0, 0, 0, 15);
    xad_huff_add(&xad_lit_huff_table, 30, 5, 1, 4, 16, 0);
    xad_huff_add(&xad_lit_huff_table, 31, 5, 1, 5, 32, 0);
}

static unsigned int xad_ceil_log2(unsigned int x) {
    unsigned int bits = 0;
    unsigned int p = 1;
    if(x < 2) { return 1; }
    while(p < x) { p = p * 2; bits = bits + 1; }
    return bits;
}

typedef struct XADB {
    const unsigned char *data;
    unsigned int size;
    unsigned int pos;
    unsigned long acc;
    unsigned int acc_bits;
} XADB;

static void xad_bits_init(XADB *br, const unsigned char *data, unsigned int size, unsigned int pos) {
    br->data = data;
    br->size = size;
    br->pos = pos;
    br->acc = 0;
    br->acc_bits = 0;
}

static int xad_bits_fill(XADB *br, unsigned int n, unsigned long *out) {
    unsigned long val = 0;
    while(br->acc_bits < n) {
        if(br->pos >= br->size) { return -1; }
        br->acc = (br->acc << 8) | (unsigned long)br->data[br->pos];
        br->pos = br->pos + 1;
        br->acc_bits = br->acc_bits + 8;
    }
    val = br->acc >> (br->acc_bits - n);
    if(br->acc_bits - n < 32) {
        br->acc = br->acc & (((unsigned long)1) << (br->acc_bits - n)) - 1;
    }
    br->acc_bits = br->acc_bits - n;
    *out = val;
    return 0;
}

static int xad_bits_get(XADB *br, unsigned int n, unsigned long *out) {
    return xad_bits_fill(br, n, out);
}

static int xad_bits_decode_huff(XADB *br, const XAHuffTable *table, unsigned int min_len, unsigned int max_len, unsigned int *out) {
    unsigned int width;
    for(width = min_len; width <= max_len; width++) {
        unsigned long code = 0;
        unsigned long value = 0;
        int found = 0;
        int i;
        if(xad_bits_fill(br, width, &code) != 0) { return -1; }
        for(i = 0; i < table->count; i++) {
            if(table->entries[i].width == width && table->entries[i].code == code) {
                if(table->entries[i].is_compact) {
                    unsigned long v = 0;
                    if(xad_bits_get(br, table->entries[i].extra_bits, &v) != 0) { return -1; }
                    value = v + table->entries[i].extra_start;
                } else {
                    value = table->entries[i].value;
                }
                *out = (unsigned int)value;
                found = 1;
                break;
            }
        }
        if(found) { return 0; }
    }
    return -1;
}

/* Decode a backward LZ distance.  magnitude is the current output position. */
static int xad_bits_decode_distance(XADB *br, unsigned int magnitude, unsigned int *out) {
    unsigned int range = magnitude;
    unsigned long b = 0;
    unsigned long v = 0;

    if(range <= 10) { return -1; }
    if(range <= 20) { return -1; }
    if(range <= 40) {
        if(xad_bits_get(br, 1, &b) != 0) { return -1; }
        if(b != 0) {
            if(xad_bits_get(br, 1, &b) != 0) { return -1; }
            if(b == 0) {
                if(xad_bits_get(br, 4, &v) != 0) { return -1; }
                *out = (unsigned int)(v + 5);
                return 0;
            }
        }
        return -1;
    }
    if(range <= 80) {
        if(xad_bits_get(br, 1, &b) != 0) { return -1; }
        if(b != 0) {
            if(xad_bits_get(br, 1, &b) != 0) { return -1; }
            if(b == 0) {
                if(xad_bits_get(br, 5, &v) != 0) { return -1; }
                *out = (unsigned int)(v + 9);
                return 0;
            }
        }
        return -1;
    }
    if(range <= 160) {
        if(xad_bits_get(br, 1, &b) != 0) { return -1; }
        if(b != 0) {
            if(xad_bits_get(br, 1, &b) != 0) { return -1; }
            if(b == 0) {
                if(xad_bits_get(br, 6, &v) != 0) { return -1; }
                *out = (unsigned int)(v + 17);
                return 0;
            }
        }
        return -1;
    }
    if(range <= 672) {
        if(xad_bits_get(br, 1, &b) != 0) { return -1; }
        if(b != 0) {
            if(xad_bits_get(br, 1, &b) != 0) { return -1; }
            if(b == 0) {
                if(xad_bits_get(br, 7, &v) != 0) { return -1; }
                *out = (unsigned int)(v + 33);
                return 0;
            }
            {
                unsigned int span = range - 160;
                unsigned long extra = 0;
                if(xad_bits_get(br, xad_ceil_log2(span), &extra) != 0) { return -1; }
                *out = (unsigned int)(extra + 161);
                return 0;
            }
        }
        if(xad_bits_get(br, 5, &v) != 0) { return -1; }
        *out = (unsigned int)(v + 1);
        return 0;
    }
    if(range <= 1000) {
        if(xad_bits_get(br, 1, &b) != 0) { return -1; }
        if(b != 0) {
            if(xad_bits_get(br, 1, &b) != 0) { return -1; }
            if(b == 0) {
                if(xad_bits_get(br, 8, &v) != 0) { return -1; }
                *out = (unsigned int)(v + 65);
                return 0;
            }
            {
                unsigned int span = range - 320;
                unsigned long extra = 0;
                if(xad_bits_get(br, xad_ceil_log2(span), &extra) != 0) { return -1; }
                *out = (unsigned int)(extra + 321);
                return 0;
            }
        }
        if(xad_bits_get(br, 6, &v) != 0) { return -1; }
        *out = (unsigned int)(v + 1);
        return 0;
    }
    if(range <= 2688) {
        if(xad_bits_get(br, 1, &b) != 0) { return -1; }
        if(b != 0) {
            if(xad_bits_get(br, 1, &b) != 0) { return -1; }
            if(b == 0) {
                if(xad_bits_get(br, 9, &v) != 0) { return -1; }
                *out = (unsigned int)(v + 129);
                return 0;
            }
            {
                unsigned int span = range - 640;
                unsigned long extra = 0;
                if(xad_bits_get(br, xad_ceil_log2(span), &extra) != 0) { return -1; }
                *out = (unsigned int)(extra + 641);
                return 0;
            }
        }
        if(xad_bits_get(br, 7, &v) != 0) { return -1; }
        *out = (unsigned int)(v + 1);
        return 0;
    }
    if(range <= 5376) {
        if(xad_bits_get(br, 1, &b) != 0) { return -1; }
        if(b != 0) {
            if(xad_bits_get(br, 1, &b) != 0) { return -1; }
            if(b == 0) {
                if(xad_bits_get(br, 10, &v) != 0) { return -1; }
                *out = (unsigned int)(v + 257);
                return 0;
            }
            {
                unsigned int span = range - 1280;
                unsigned long extra = 0;
                if(xad_bits_get(br, xad_ceil_log2(span), &extra) != 0) { return -1; }
                *out = (unsigned int)(extra + 1281);
                return 0;
            }
        }
        if(xad_bits_get(br, 8, &v) != 0) { return -1; }
        *out = (unsigned int)(v + 1);
        return 0;
    }
    if(range <= 10752) {
        if(xad_bits_get(br, 1, &b) != 0) { return -1; }
        if(b != 0) {
            if(xad_bits_get(br, 1, &b) != 0) { return -1; }
            if(b != 0) {
                unsigned int span = range - 2560;
                unsigned long extra = 0;
                if(xad_bits_get(br, xad_ceil_log2(span), &extra) != 0) { return -1; }
                *out = (unsigned int)(extra + 2561);
                return 0;
            }
            if(xad_bits_get(br, 11, &v) != 0) { return -1; }
            *out = (unsigned int)(v + 513);
            return 0;
        }
        if(xad_bits_get(br, 9, &v) != 0) { return -1; }
        *out = (unsigned int)(v + 1);
        return 0;
    }
    return -1;
}

int xad_instacomp_one_looks_like(const unsigned char *src, unsigned int src_size) {
    if(src_size < 14) { return 0; }
    if(xad_be32(src, 0) != XAD_INSTA_COMP_MAGIC) { return 0; }
    if(xad_be16(src, 4) != XAD_INSTA_COMP_HEADER_LEN) { return 0; }
    return 1;
}

int xad_instacomp_one_unpack(const unsigned char *src, unsigned int src_size, unsigned char **out, unsigned int *out_size) {
    unsigned int unpack_size = 0;
    XADB br;
    unsigned char *dst = 0;
    unsigned int dst_len = 0;
    unsigned int mode = 1;

    xad_huff_build();
    if(src_size < 14) { return -1; }
    if(xad_be32(src, 0) != XAD_INSTA_COMP_MAGIC) { return -1; }
    if(xad_be16(src, 4) != XAD_INSTA_COMP_HEADER_LEN) { return -1; }
    if(src[6] != XAD_INSTA_COMP_VERSION) { return -1; }
    if(src[7] != XAD_INSTA_COMP_FLAG) { return -1; }
    if(xad_be16(src, 12) != XAD_INSTA_COMP_CODEC_ID) { return -1; }
    unpack_size = xad_be32(src, 8);

    dst = (unsigned char *)malloc(unpack_size);
    if(dst == 0) { return -1; }

    xad_bits_init(&br, src, src_size, XAD_INSTA_COMP_SKIP_BYTES);
    while(dst_len < unpack_size) {
        unsigned int copy_count = 0;
        unsigned int lit_len = 0;
        if(xad_bits_decode_huff(&br, &xad_len_huff_table, 2, 11, &copy_count) != 0) { free(dst); return -1; }
        if(copy_count > 0 || mode == 0) {
            unsigned int distance = 0;
            unsigned int ref_pos = 0;
            unsigned int i;
            copy_count = copy_count + 2;
            if(mode == 0) { copy_count = copy_count + 1; }
            if(xad_bits_decode_distance(&br, dst_len, &distance) != 0) { free(dst); return -1; }
            ref_pos = dst_len - distance;
            if(ref_pos > dst_len) { free(dst); return -1; }
            if(dst_len + copy_count > unpack_size) { free(dst); return -1; }
            for(i = 0; i < copy_count; i++) { dst[dst_len + i] = dst[ref_pos + i]; }
            dst_len = dst_len + copy_count;
            mode = 1;
        } else {
            if(xad_bits_decode_huff(&br, &xad_lit_huff_table, 1, 7, &lit_len) != 0) { free(dst); return -1; }
            if(dst_len + lit_len > unpack_size) { free(dst); return -1; }
            {
                unsigned int i;
                for(i = 0; i < lit_len; i++) {
                    unsigned long b = 0;
                    if(xad_bits_get(&br, 8, &b) != 0) { free(dst); return -1; }
                    dst[dst_len] = (unsigned char)(b & 0xFF);
                    dst_len = dst_len + 1;
                }
            }
            mode = (lit_len < XAD_LIT_MAX_LEN) ? 0 : 1;
        }
    }
    *out = dst;
    *out_size = unpack_size;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Apple installer Tome container: metadata and fork decoding         */
/* ------------------------------------------------------------------ */

static int xad_tome_strcasecmp(const char *a, const char *b) {
    unsigned int i = 0;
    while((a[i] != 0 && b[i] != 0) && (unsigned char)a[i] == (unsigned char)b[i]) { i = i + 1; }
    if(a[i] == b[i]) { return 0; }
    return (int)(32 - (unsigned char)a[i] + (unsigned char)b[i]);
}

int xad_tome_looks_like(const unsigned char *data, unsigned int size) {
    if(size < TOME_HEADER_SIZE) { return 0; }
    if(xad_be32(data, 0) != TOME_MAGIC) { return 0; }
    return 1;
}

static void xad_tome_set_name(char *out, const unsigned char *sec, unsigned int name_len) {
    unsigned int n = name_len;
    unsigned int i;
    if(n > 31) { n = 31; }
    for(i = 0; i < n; i++) { out[i] = (char)sec[7 + i]; }
    out[n] = 0;
}

int xad_tome_open(const unsigned char *data, unsigned int size, XADTomeArchive *out) {
    XADTomeFile *files = 0;
    unsigned int count = 0;
    int i;

    if(xad_tome_looks_like(data, size) == 0) { return -1; }
    count = xad_be32(data, 28);
    if(count <= 0) { return -1; }
    if((unsigned int)(TOME_HEADER_SIZE + (unsigned int)count * TOME_SECTION_SIZE) > size) { return -1; }

    files = (XADTomeFile *)malloc(sizeof(XADTomeFile) * (unsigned int)count);
    if(files == 0) { return -1; }
    for(i = 0; i < (int)count; i++) {
        unsigned char *sec = (unsigned char *)(data + TOME_HEADER_SIZE + (unsigned int)i * TOME_SECTION_SIZE);
        XADTomeFile *f = &files[i];
        unsigned int name_len = sec[6];
        f->t_id = (xadUINT16)xad_be16(sec, 4);
        xad_tome_set_name(f->t_name, sec, name_len);
        f->t_type = xad_be32(sec, 38);
        f->t_creator = xad_be32(sec, 42);
        f->t_version = (xadUINT32)xad_be16(sec, 54);
        f->t_data_size = xad_be32(sec, 60);
        f->t_data_offset = xad_be32(sec, 64);
        f->t_data_compressed_size = xad_be32(sec, 68);
        f->t_resource_size = xad_be32(sec, 76);
        f->t_resource_offset = xad_be32(sec, 80);
        f->t_resource_compressed_size = xad_be32(sec, 84);
    }

    out->files = files;
    out->file_count = (int)count;
    out->data = (unsigned char *)data;
    out->data_size = size;
    return 0;
}

void xad_tome_free(XADTomeArchive *arc) {
    if(arc == 0) { return; }
    if(arc->files != 0) { free(arc->files); }
    arc->files = 0;
    arc->file_count = 0;
    arc->data = 0;
    arc->data_size = 0;
}

static int xad_tome_starts_with(const unsigned char *data, unsigned int size, const char *prefix, unsigned int prefix_len) {
    unsigned int i;
    if(size < prefix_len) { return 0; }
    for(i = 0; i < prefix_len; i++) {
        if((unsigned char)prefix[i] != data[i]) { return 0; }
    }
    return 1;
}

static int xad_tome_starts_with_bytes(const unsigned char *data, unsigned int size, const unsigned char *prefix, unsigned int prefix_len) {
    unsigned int i;
    if(size < prefix_len) { return 0; }
    for(i = 0; i < prefix_len; i++) {
        if(prefix[i] != data[i]) { return 0; }
    }
    return 1;
}

/* Unwrap one fork.  Returns 0 on success, -1 on failure. */
int xad_tome_unwrap(const unsigned char *compressed, unsigned int compressed_size, unsigned int uncompressed_size, unsigned char **out, unsigned int *out_size) {
    unsigned char *result = 0;
    unsigned char *unpacked = 0;
    unsigned int unpacked_size = 0;
    unsigned int result_size = 0;
    int ok = 0;
    unsigned int i;

    if(uncompressed_size == 0) {
        *out = (unsigned char *)malloc(1);
        if(*out == 0) { return -1; }
        *out_size = 0;
        return 0;
    }
    if(compressed_size == uncompressed_size) {
        result = (unsigned char *)malloc(compressed_size);
        if(result == 0) { return -1; }
        for(i = 0; i < compressed_size; i++) { result[i] = compressed[i]; }
        result_size = compressed_size;
        ok = 1;
    } else if(xad_instacomp_one_looks_like(compressed, compressed_size) != 0) {
        if(xad_instacomp_one_unpack(compressed, compressed_size, &unpacked, &unpacked_size) == 0) {
            result_size = (unpacked_size < uncompressed_size) ? unpacked_size : uncompressed_size;
            result = (unsigned char *)malloc(result_size);
            if(result == 0) { free(unpacked); return -1; }
            for(i = 0; i < result_size; i++) { result[i] = unpacked[i]; }
            free(unpacked);
            ok = 1;
        }
    }
    if(ok == 0) {
        if(compressed_size > uncompressed_size) {
            if(compressed_size >= (4 + uncompressed_size)) {
                if(xad_tome_starts_with_bytes(compressed, 4, xad_tome_stored_prefix, 4) == 1) {
                    result = (unsigned char *)malloc(uncompressed_size);
                    if(result == 0) { return -1; }
                    for(i = 0; i < uncompressed_size; i++) { result[i] = compressed[4 + i]; }
                    result_size = uncompressed_size;
                    ok = 1;
                } else if(xad_tome_starts_with(compressed, compressed_size, xad_tome_chrp_prefix, TOME_CHRP_PREFIX_LEN) == 1) {
                    result = (unsigned char *)malloc(uncompressed_size);
                    if(result == 0) { return -1; }
                    for(i = 0; i < uncompressed_size; i++) { result[i] = compressed[i]; }
                    result_size = uncompressed_size;
                    ok = 1;
                }
            }
            if(ok == 0 && compressed_size >= uncompressed_size) {
                result = (unsigned char *)malloc(uncompressed_size);
                if(result == 0) { return -1; }
                for(i = 0; i < uncompressed_size; i++) { result[i] = compressed[i]; }
                result_size = uncompressed_size;
                ok = 1;
            }
        }
    }
    if(ok == 0) {
        /* Last resort: treat the fork as an InstaCompOne stream. */
        unsigned char *fallback = 0;
        unsigned int fallback_size = 0;
        if(xad_instacomp_one_unpack(compressed, compressed_size, &fallback, &fallback_size) == 0) {
            result = fallback;
            result_size = fallback_size;
            ok = 1;
        }
    }
    if(ok == 0) { return -1; }
    *out = result;
    *out_size = result_size;
    return 0;
}

int xad_tome_data_fork(const XADTomeArchive *arc, const XADTomeFile *file, unsigned char **out, unsigned int *out_size) {
    unsigned int uncompressed_size = 0;
    unsigned int compressed_size = 0;
    const unsigned char *compressed = 0;
    unsigned char *result = 0;
    unsigned int result_size = 0;

    if(arc == 0) { return -1; }
    if(file == 0) { return -1; }
    uncompressed_size = file->t_data_size;
    compressed_size = file->t_data_compressed_size;
    if((unsigned int)(file->t_data_offset + compressed_size) > arc->data_size) { return -1; }
    compressed = arc->data + file->t_data_offset;
    if(xad_tome_unwrap(compressed, compressed_size, uncompressed_size, &result, &result_size) != 0) { return -1; }
    *out = result;
    *out_size = result_size;
    return 0;
}

int xad_tome_resource_fork(const XADTomeArchive *arc, const XADTomeFile *file, unsigned char **out, unsigned int *out_size) {
    unsigned int uncompressed_size = 0;
    unsigned int compressed_size = 0;
    const unsigned char *compressed = 0;
    unsigned char *result = 0;
    unsigned int result_size = 0;

    if(arc == 0) { return -1; }
    if(file == 0) { return -1; }
    uncompressed_size = file->t_resource_size;
    compressed_size = file->t_resource_compressed_size;
    if((unsigned int)(file->t_resource_offset + compressed_size) > arc->data_size) { return -1; }
    compressed = arc->data + file->t_resource_offset;
    if(xad_tome_unwrap(compressed, compressed_size, uncompressed_size, &result, &result_size) != 0) { return -1; }
    *out = result;
    *out_size = result_size;
    return 0;
}

int xad_tome_select_rom(const XADTomeArchive *arc, XADTomeFile **out) {
    int i;
    if(arc == 0) { return -1; }
    for(i = 0; i < arc->file_count; i++) {
        if((unsigned int)arc->files[i].t_type == 0x74627869) { /* "tbxi" */
            *out = &arc->files[i];
            return 0;
        }
    }
    for(i = 0; i < arc->file_count; i++) {
        if(xad_tome_strcasecmp(arc->files[i].t_name, xad_tome_rom_name) == 0) {
            *out = &arc->files[i];
            return 0;
        }
    }
    for(i = 0; i < arc->file_count; i++) {
        const char *p = arc->files[i].t_name;
        int is_rom = 0;
        while(*p) { if(*p == 'R' || *p == 'r') { is_rom = 1; } p = p + 1; }
        if(is_rom) { *out = &arc->files[i]; return 0; }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* MacBinary II / III wrapper parsing                                 */
/* ------------------------------------------------------------------ */

static unsigned int xad_mac_pad128(unsigned int n) {
    return (n + 127) / 128 * 128;
}

int xad_macbinary_looks_like(const unsigned char *data, unsigned int size) {
    unsigned int name_length = 0;
    unsigned int version = 0;
    unsigned int data_len = 0;
    unsigned int rsrc_len = 0;
    if(size < 128) { return 0; }
    if(data[0] != 0 || data[74] != 0) { return 0; }
    name_length = data[1];
    if(name_length < 1 || name_length > 63) { return 0; }
    if(data[82] != 0) { return 0; }
    version = data[122];
    if(version != 0 && version != 129 && version != 130) { return 0; }
    data_len = xad_be32(data, 83);
    rsrc_len = xad_be32(data, 87);
    if((data_len + rsrc_len) == 0) { return 0; }
    if(128 + xad_mac_pad128(data_len) > size) { return 0; }
    return 1;
}

int xad_macbinary_parse(const unsigned char *data, unsigned int size, XADMacBinaryFile *out) {
    unsigned int name_length = 0;
    unsigned int data_len = 0;
    unsigned int rsrc_len = 0;
    unsigned int rsrc_offset = 0;
    unsigned int i = 0;

    if(size < 128) { return -1; }
    if(xad_macbinary_looks_like(data, size) == 0) { return -1; }
    name_length = data[1];
    for(i = 0; i < name_length; i++) { out->file_name[i] = (char)data[2 + i]; }
    out->file_name[name_length] = 0;
    for(i = 0; i < 4; i++) { out->file_type[i] = (char)data[65 + i]; }
    out->file_type[4] = 0;
    for(i = 0; i < 4; i++) { out->creator[i] = (char)data[69 + i]; }
    out->creator[4] = 0;
    data_len = xad_be32(data, 83);
    rsrc_len = xad_be32(data, 87);
    out->data_fork = (unsigned char *)malloc(data_len > 0 ? data_len : 1);
    if(out->data_fork == 0) { return -1; }
    for(i = 0; i < data_len; i++) { out->data_fork[i] = data[128 + i]; }
    out->data_fork_size = data_len;
    rsrc_offset = 128 + xad_mac_pad128(data_len);
    if(rsrc_len == 0) {
        out->resource_fork = (unsigned char *)malloc(1);
        out->resource_fork_size = 0;
    } else if(rsrc_offset + rsrc_len <= size) {
        out->resource_fork = (unsigned char *)malloc(rsrc_len);
        if(out->resource_fork == 0) { return -1; }
        for(i = 0; i < rsrc_len; i++) { out->resource_fork[i] = data[rsrc_offset + i]; }
        out->resource_fork_size = rsrc_len;
    } else if(128 + data_len + rsrc_len <= size) {
        /* Some writers skip the 128-byte pad. */
        out->resource_fork = (unsigned char *)malloc(rsrc_len);
        if(out->resource_fork == 0) { return -1; }
        for(i = 0; i < rsrc_len; i++) { out->resource_fork[i] = data[128 + data_len + i]; }
        out->resource_fork_size = rsrc_len;
    } else {
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Client: file output and archive install                           */
/* ------------------------------------------------------------------ */

static int xad_client_write_file(const char *dir, const char *name, const unsigned char *data, unsigned int size) {
    char path[1024];
    FILE *f;

    sprintf(path, "%s/%s", dir, name);
    mkdir(dir, 0777);
    f = fopen(path, "wb");
    if(f == 0) { return -1; }
    if(size > 0) {
        if(fwrite(data, 1, size, f) != size) { fclose(f); return -1; }
    }
    if(fclose(f) != 0) { return -1; }
    return 0;
}

int xad_tome_client_install(const unsigned char *data, unsigned int size, const char *output_dir) {
    XADTomeArchive arc;
    XADTomeFile *rom_file = 0;
    unsigned char *rom = 0;
    unsigned int rom_size = 0;
    int status = -1;

    if(xad_tome_looks_like(data, size) == 1) {
        if(xad_tome_open(data, size, &arc) == 0) {
            if(xad_tome_select_rom(&arc, &rom_file) == 0) {
                if(xad_tome_data_fork(&arc, rom_file, &rom, &rom_size) == 0) {
                    status = xad_client_write_file(output_dir, xad_tome_rom_name, rom, rom_size);
                    free(rom);
                }
            }
            xad_tome_free(&arc);
        }
        return status;
    }
    if(xad_macbinary_looks_like(data, size) == 1) {
        XADMacBinaryFile mb;
        memset(&mb, 0, sizeof(mb));
        if(xad_macbinary_parse(data, size, &mb) == 0) {
            if(xad_tome_looks_like(mb.data_fork, mb.data_fork_size) == 1) {
                if(xad_tome_open(mb.data_fork, mb.data_fork_size, &arc) == 0) {
                    if(xad_tome_select_rom(&arc, &rom_file) == 0) {
                        if(xad_tome_data_fork(&arc, rom_file, &rom, &rom_size) == 0) {
                            status = xad_client_write_file(output_dir, xad_tome_rom_name, rom, rom_size);
                            free(rom);
                        }
                    }
                    xad_tome_free(&arc);
                    free(mb.data_fork);
                }
            }
        }
    }
    return -1;
}

int xad_tome_client_recognize(const unsigned char *data, unsigned int size) {
    if(size < TOME_HEADER_SIZE) { return 0; }
    if(xad_be32(data, 0) != TOME_MAGIC) { return 0; }
    return 1;
}

const char *xad_tome_client_name(void) {
    return TOME_ARCHIVERNAME;
}

/* ------------------------------------------------------------------ */
/* XAD client protocol functions                                       */
/* ------------------------------------------------------------------ */

XADRECOGDATA(tome)
{
    (void)ai;
    if((unsigned int)size < (unsigned int)TOME_HEADER_SIZE) { return XADFALSE; }
    if(xad_be32(data, 0) != TOME_MAGIC) { return XADFALSE; }
    return XADTRUE;
}

XADGETINFO(tome)
{
    struct TomePrivate *priv = (struct TomePrivate *)malloc(sizeof(struct TomePrivate));
    struct xadFileInfo *fi = (struct xadFileInfo *)malloc(sizeof(struct xadFileInfo));
    int i = 0;

    if(ai == 0) { return XADERR_BADPARAMS; }
    if(priv == 0) { return XADERR_NOMEMORY; }
    if(fi == 0) { free(priv); return XADERR_NOMEMORY; }

    priv->archive.files = 0;
    priv->archive.file_count = 0;
    priv->archive.data = 0;
    priv->archive.data_size = 0;
    priv->file = 0;
    for(i = 0; i < 511; i++) { priv->outDir[i] = 0; }
    priv->outDir[0] = '.';
    priv->outDir[1] = 0;

    memset(fi, 0, sizeof(*fi));
    fi->xfi_Next = 0;
    fi->xfi_EntryNumber = 1;
    fi->xfi_EntryInfo = (xadSTRPTR)(const char *)"tbxi installer ROM";
    fi->xfi_PrivateInfo = priv;
    fi->xfi_Flags = XADFIF_SEEKDATAPOS | XADFIF_EXTRACTONBUILD;
    fi->xfi_FileName = (xadSTRPTR)(xad_tome_rom_name);
    fi->xfi_Comment = 0;
    fi->xfi_Protection = 0;
    fi->xfi_OwnerUID = 0;
    fi->xfi_OwnerGID = 0;
    fi->xfi_UserName = 0;
    fi->xfi_GroupName = 0;
    fi->xfi_Size = 0;
    fi->xfi_GroupCrSize = 0;
    fi->xfi_CrunchSize = 0;
    fi->xfi_LinkName = 0;
    fi->xfi_Generation = 0;
    fi->xfi_DataPos = 0;
    fi->xfi_MacFork = 0;
    fi->xfi_UnixProtect = 0;
    fi->xfi_DosProtect = 0;
    fi->xfi_FileType = 0;

    ai->xai_FileInfo = fi;
    ai->xai_CurFile = fi;
    ai->xai_Flags = 0;
    ai->xai_LastError = XADERR_OK;
    return XADERR_OK;
}

XADUNARCHIVE(tome)
{
    struct xadFileInfo *fi = 0;
    struct TomePrivate *priv = 0;
    unsigned char *fork = 0;
    unsigned int fork_size = 0;
    xadERROR err = XADERR_OK;

    if(ai == 0) { return XADERR_BADPARAMS; }
    fi = ai->xai_CurFile;
    if(fi == 0) { return XADERR_BADPARAMS; }
    priv = (struct TomePrivate *)fi->xfi_PrivateInfo;
    if(priv == 0) { return XADERR_BADPARAMS; }
    if(priv->file == 0) { return XADERR_DATAFORMAT; }
    if(priv->outDir[0] == 0) {
        priv->outDir[0] = '.';
        priv->outDir[1] = 0;
    }
    if(xad_tome_data_fork(&priv->archive, priv->file, &fork, &fork_size) == 0) {
        if(xad_client_write_file(priv->outDir, xad_tome_rom_name, fork, fork_size) != 0) {
            err = XADERR_OUTPUT;
        }
        free(fork);
    } else {
        err = XADERR_DATAFORMAT;
    }
    return err;
}

XADFREE(tome)
{
    struct xadFileInfo *fi = ai->xai_FileInfo;

    if(ai == 0) { return; }
    while(fi != 0) {
        struct xadFileInfo *next = fi->xfi_Next;
        if(fi->xfi_PrivateInfo != 0) {
            struct TomePrivate *priv = (struct TomePrivate *)fi->xfi_PrivateInfo;
            if(priv != 0) {
                if(priv->archive.files != 0) { free(priv->archive.files); }
                if(priv->archive.data != 0) { free(priv->archive.data); }
                free(priv);
            }
        }
        fi = next;
    }
}

/* ------------------------------------------------------------------ */
/* Library registration                                                */
/* ------------------------------------------------------------------ */

XADFIRSTCLIENT(tome)

const struct xadClient *xad_tome_first_client(void) {
    return &FirstClient_tome;
}
