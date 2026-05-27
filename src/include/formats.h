#ifndef THARAVU_FORMATS_H
#define THARAVU_FORMATS_H

#include "tharavu_types.h"

#pragma pack(push, 1)

typedef struct {
    char     magic[4];
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t row_count;
    uint16_t col_count;
    uint16_t flags;
    uint32_t row_stride;
    uint32_t hash_cap;
    uint64_t schema_offset;
    uint64_t data_offset;
    uint64_t hash_offset;
    uint64_t file_size;
    uint64_t created_at;
    uint64_t modified_at;
    char     name[32];
    char     reserved[24];
} odat_header_t;

typedef struct {
    char     magic[4];          /*   0 */
    uint16_t version_major;     /*   4 */
    uint16_t version_minor;     /*   6 */
    uint32_t vocab_count;       /*   8 */
    uint32_t dim;               /*  12 */
    uint32_t flags;             /*  16 */
    uint32_t hash_cap;          /*  20 */
    uint64_t hash_offset;       /*  24 */
    uint64_t data_offset;       /*  32 */
    uint64_t file_size;         /*  40 */
    uint64_t created_at;        /*  48 */
    uint64_t modified_at;       /*  56 */
    char     name[32];          /*  64 */
    uint64_t reverse_offset;    /*  96  -- offset of uint64_t[vocab_count] reverse index;
                                          0 in v1.0 files that predate this field */
    char     reserved[24];      /* 104 */
} ovoc_header_t;                /* 128 bytes total */

typedef struct {
    char     magic[4];
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t dim;
    uint32_t stride;
    uint32_t row_count;
    uint32_t flags;
    uint64_t data_offset;
    uint64_t file_size;
    uint64_t created_at;
    uint64_t modified_at;
    char     name[32];
    char     source[32];
    char     reserved[8];
} ovec_header_t;

#pragma pack(pop)

/* Compile-time checks */
_Static_assert(sizeof(odat_header_t) == 128, "ODAT Header must be 128 bytes");
_Static_assert(sizeof(ovoc_header_t) == 128, "OVOC Header must be 128 bytes");
_Static_assert(sizeof(ovec_header_t) == 128, "OVEC Header must be 128 bytes");

#endif