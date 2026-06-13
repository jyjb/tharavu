/*
 * ogph.c  —  OGPH graph file reader
 *
 * Implements tgph_* API declared in ogph.h.
 * File format: CSR layout with 64-byte header, node table, CSR row pointers,
 * edge array, string pool, and optional embedding block.
 * Node lookup is O(1) via an FNV-1a hash index built at open time.
 */

#include "include/ogph.h"
#include "include/ogph_builder.h"
#include "include/data_engine.h"
#include "include/tharavu_dll.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#  include <windows.h>
#  define ogph_unlink _unlink
#else
#  include <unistd.h>
#  define ogph_unlink unlink
#endif

/* ── On-disk structures (packed, little-endian) ───────────────────────────── */

#pragma pack(push, 1)

typedef struct {
    char     magic[4];           /* "OGPH"            */
    uint16_t abi_version;        /* 1                  */
    uint16_t flags;              /* OGPH_FLAG_*        */
    uint32_t node_count;
    uint32_t edge_count;
    uint32_t node_prop_dim;      /* embed dim; 0 = none */
    uint32_t string_pool_size;   /* bytes              */
    char     reserved[40];       /* zeroed             */
} ogph_header_t;                 /* 64 bytes total     */

_Static_assert(sizeof(ogph_header_t) == 64, "OGPH header must be 64 bytes");

typedef struct {
    uint64_t node_id;
    uint32_t type_id;
    uint32_t label_off;   /* byte offset into string pool            */
    uint32_t embed_off;   /* byte offset into embed block; 0xFFFFFFFF = none */
    uint32_t edge_start;  /* == row_ptr[i]; redundant, kept for convenience */
    uint32_t node_flags;
    uint32_t _pad;        /* alignment to 32 bytes                   */
} ogph_node_rec_t;        /* 32 bytes total                          */

_Static_assert(sizeof(ogph_node_rec_t) == 32, "OGPH node record must be 32 bytes");

typedef struct {
    uint32_t target_node; /* node table index          */
    uint16_t type_id;     /* OGPH_EDGE_*               */
    uint16_t edge_flags;
    float    weight;      /* 0.0–1.0                   */
    uint32_t reserved;
} ogph_edge_rec_t;        /* 16 bytes total            */

_Static_assert(sizeof(ogph_edge_rec_t) == 16, "OGPH edge record must be 16 bytes");

#pragma pack(pop)

/* ── Opaque graph handle ──────────────────────────────────────────────────── */

struct ogph_graph_s {
    /* mmap region */
    void   *mmap_ptr;
    size_t  mmap_size;
    int     mmap_fd;

    /* header fields (copied for fast access) */
    uint32_t node_count;
    uint32_t edge_count;
    uint32_t node_prop_dim;
    uint16_t flags;

    /* zero-copy pointers directly into mmap */
    const ogph_node_rec_t *nodes;
    const uint32_t        *row_ptr;     /* [node_count + 1] */
    const ogph_edge_rec_t *edges;
    const char            *string_pool;
    const float           *embed_block; /* NULL if node_prop_dim == 0 */

    /* heap-allocated FNV-1a hash index: node_id → node_index */
    int32_t  *hash_table;   /* -1 = empty slot */
    uint32_t  hash_cap;     /* power-of-2      */
};

/* ── FNV-1a hash for uint64_t ─────────────────────────────────────────────── */

static uint32_t fnv1a_u64(uint64_t v)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 8; i++) {
        h ^= (uint8_t)(v >> (i * 8));
        h *= 16777619u;
    }
    return h;
}

/* ── Hash index construction ──────────────────────────────────────────────── */

static int build_hash_index(struct ogph_graph_s *g)
{
    /* capacity: next power-of-2 >= node_count * 2 (load factor ~0.5) */
    uint32_t cap = 16;
    while (cap < g->node_count * 2u + 1u)
        cap *= 2u;

    g->hash_table = (int32_t *)malloc(cap * sizeof(int32_t));
    if (!g->hash_table) return TDE_ERR_MEM;

    for (uint32_t i = 0; i < cap; i++)
        g->hash_table[i] = -1;

    g->hash_cap = cap;

    for (uint32_t i = 0; i < g->node_count; i++) {
        uint64_t nid   = g->nodes[i].node_id;
        uint32_t slot  = fnv1a_u64(nid) & (cap - 1u);
        uint32_t probe = 0;
        while (probe < cap) {
            if (g->hash_table[slot] == -1) {
                g->hash_table[slot] = (int32_t)i;
                break;
            }
            slot = (slot + 1u) & (cap - 1u);
            probe++;
        }
        if (probe >= cap) {
            free(g->hash_table);
            g->hash_table = NULL;
            return TDE_ERR_CORRUPT;
        }
    }
    return TDE_OK;
}

/* ── Logical name resolution (.ogph) ─────────────────────────────────────── */

static int ogph_resolve_path(const char *logical_name, char *out_path, size_t max_len)
{
    return de_resolve_path(logical_name, 4 /* TDE_FILE_OGPH */, out_path, max_len);
}

/* ── tgph_open ────────────────────────────────────────────────────────────── */

THARAVU_API ogph_graph_t * THARAVU_CALL tgph_open(const char *path)
{
    if (!path) return NULL;

    void  *addr = NULL;
    size_t len  = 0;
    int    fd   = -1;

    if (de_platform_mmap_readonly(path, &addr, &len, &fd) != TDE_OK)
        return NULL;

    /* Minimum size: 64-byte header */
    if (len < sizeof(ogph_header_t)) {
        de_platform_unmap(addr, len, fd);
        return NULL;
    }

    const ogph_header_t *hdr = (const ogph_header_t *)addr;

    if (memcmp(hdr->magic, "OGPH", 4) != 0 || hdr->abi_version != 1) {
        de_platform_unmap(addr, len, fd);
        return NULL;
    }

    uint32_t nc   = hdr->node_count;
    uint32_t ec   = hdr->edge_count;
    uint32_t dim  = hdr->node_prop_dim;
    uint32_t pool = hdr->string_pool_size;

    /* Compute section offsets and validate they fit within the file */
    uint64_t node_off    = sizeof(ogph_header_t);
    uint64_t rowptr_off  = node_off   + (uint64_t)nc * sizeof(ogph_node_rec_t);
    uint64_t edge_off    = rowptr_off + (uint64_t)(nc + 1u) * sizeof(uint32_t);
    uint64_t strpool_off = edge_off   + (uint64_t)ec * sizeof(ogph_edge_rec_t);
    uint64_t embed_off   = strpool_off + pool;

    /* embed block size: upper bound = nc * dim * 4 (in practice may be less if
     * not all nodes have embeddings, but we only need the block to start at
     * embed_off and we validate individual embed_off values on access). */

    if (strpool_off > len || embed_off > len) {
        de_platform_unmap(addr, len, fd);
        return NULL;
    }

    struct ogph_graph_s *g = (struct ogph_graph_s *)calloc(1, sizeof(*g));
    if (!g) {
        de_platform_unmap(addr, len, fd);
        return NULL;
    }

    g->mmap_ptr      = addr;
    g->mmap_size     = len;
    g->mmap_fd       = fd;
    g->node_count    = nc;
    g->edge_count    = ec;
    g->node_prop_dim = dim;
    g->flags         = hdr->flags;

    uint8_t *base = (uint8_t *)addr;
    g->nodes       = (const ogph_node_rec_t *)(base + node_off);
    g->row_ptr     = (const uint32_t         *)(base + rowptr_off);
    g->edges       = (const ogph_edge_rec_t  *)(base + edge_off);
    g->string_pool = (const char             *)(base + strpool_off);
    g->embed_block = (dim > 0 && embed_off < len)
                     ? (const float *)(base + embed_off)
                     : NULL;

    if (build_hash_index(g) != TDE_OK) {
        de_platform_unmap(addr, len, fd);
        free(g);
        return NULL;
    }

    return g;
}

THARAVU_API ogph_graph_t * THARAVU_CALL tgph_open_logical(const char *logical_name)
{
    if (!logical_name) return NULL;
    char path[MAX_PATH_LEN];
    if (ogph_resolve_path(logical_name, path, sizeof(path)) != TDE_OK)
        return NULL;
    return tgph_open(path);
}

/* ── tgph_save ────────────────────────────────────────────────────────────── */

THARAVU_API int THARAVU_CALL tgph_save(const ogph_graph_t *g, const char *path)
{
    if (!g || !path) return TDE_ERR_INVAL;

    char temp[MAX_PATH_LEN];
    if (snprintf(temp, sizeof(temp), "%s.tmp", path) >= (int)sizeof(temp))
        return TDE_ERR_INVAL;

    FILE *fp = fopen(temp, "wb");
    if (!fp) return TDE_ERR_IO;

    if (fwrite(g->mmap_ptr, 1, g->mmap_size, fp) != g->mmap_size) {
        fclose(fp);
        ogph_unlink(temp);
        return TDE_ERR_IO;
    }
    fclose(fp);

#ifdef _WIN32
    if (!de_platform_atomic_rename(temp, path)) {
        ogph_unlink(temp);
        return TDE_ERR_IO;
    }
#else
    if (rename(temp, path) != 0) {
        ogph_unlink(temp);
        return TDE_ERR_IO;
    }
#endif
    return TDE_OK;
}

THARAVU_API int THARAVU_CALL tgph_save_logical(const ogph_graph_t *g, const char *logical_name)
{
    if (!g || !logical_name) return TDE_ERR_INVAL;
    char path[MAX_PATH_LEN];
    if (ogph_resolve_path(logical_name, path, sizeof(path)) != TDE_OK)
        return TDE_ERR_INVAL;
    return tgph_save(g, path);
}

/* ── tgph_close ───────────────────────────────────────────────────────────── */

THARAVU_API void THARAVU_CALL tgph_close(ogph_graph_t *g)
{
    if (!g) return;
    if (g->mmap_ptr)
        de_platform_unmap(g->mmap_ptr, g->mmap_size, g->mmap_fd);
    free(g->hash_table);
    free(g);
}

/* ── Graph metadata ───────────────────────────────────────────────────────── */

THARAVU_API int      THARAVU_CALL tgph_node_count      (const ogph_graph_t *g) { return g ? (int)g->node_count    : 0; }
THARAVU_API int      THARAVU_CALL tgph_edge_count_total(const ogph_graph_t *g) { return g ? (int)g->edge_count     : 0; }
THARAVU_API uint16_t THARAVU_CALL tgph_flags           (const ogph_graph_t *g) { return g ? g->flags               : 0; }
THARAVU_API int      THARAVU_CALL tgph_embed_dim       (const ogph_graph_t *g) { return g ? (int)g->node_prop_dim  : 0; }

/* ── Node queries ─────────────────────────────────────────────────────────── */

THARAVU_API int THARAVU_CALL tgph_find_node(const ogph_graph_t *g, uint64_t node_id)
{
    if (!g || g->node_count == 0) return -1;

    uint32_t slot  = fnv1a_u64(node_id) & (g->hash_cap - 1u);
    uint32_t probe = 0;
    while (probe < g->hash_cap) {
        int32_t idx = g->hash_table[slot];
        if (idx == -1) return -1;
        if (g->nodes[idx].node_id == node_id) return idx;
        slot = (slot + 1u) & (g->hash_cap - 1u);
        probe++;
    }
    return -1;
}

THARAVU_API uint64_t THARAVU_CALL tgph_node_id(const ogph_graph_t *g, int idx)
{
    if (!g || idx < 0 || idx >= (int)g->node_count) return 0;
    return g->nodes[idx].node_id;
}

THARAVU_API int THARAVU_CALL tgph_node_type(const ogph_graph_t *g, int idx)
{
    if (!g || idx < 0 || idx >= (int)g->node_count) return 0;
    return (int)g->nodes[idx].type_id;
}

THARAVU_API const char * THARAVU_CALL tgph_node_label(const ogph_graph_t *g, int idx)
{
    if (!g || idx < 0 || idx >= (int)g->node_count) return NULL;
    uint32_t off = g->nodes[idx].label_off;
    if (off >= g->mmap_size) return NULL; /* corrupt guard */
    return g->string_pool + off;
}

THARAVU_API uint32_t THARAVU_CALL tgph_node_flags(const ogph_graph_t *g, int idx)
{
    if (!g || idx < 0 || idx >= (int)g->node_count) return 0;
    return g->nodes[idx].node_flags;
}

THARAVU_API const float * THARAVU_CALL tgph_node_embed(const ogph_graph_t *g, int idx)
{
    if (!g || idx < 0 || idx >= (int)g->node_count) return NULL;
    if (!g->embed_block || g->node_prop_dim == 0) return NULL;
    uint32_t off = g->nodes[idx].embed_off;
    if (off == 0xFFFFFFFFu) return NULL;
    return (const float *)((const char *)g->embed_block + off);
}

/* ── Edge traversal ───────────────────────────────────────────────────────── */

THARAVU_API int THARAVU_CALL tgph_next_edge(const ogph_graph_t *g,
                                             int      node_idx,
                                             int     *edge_cursor,
                                             int     *target_idx,
                                             uint16_t *type_id,
                                             float    *weight)
{
    if (!g || !edge_cursor) return 0;
    if (node_idx < 0 || node_idx >= (int)g->node_count) return 0;

    uint32_t start = g->row_ptr[node_idx];
    uint32_t end   = g->row_ptr[node_idx + 1];
    uint32_t pos   = start + (uint32_t)(*edge_cursor);
    if (pos >= end) return 0;

    const ogph_edge_rec_t *e = &g->edges[pos];
    if (target_idx) *target_idx = (int)e->target_node;
    if (type_id)    *type_id    = e->type_id;
    if (weight)     *weight     = e->weight;
    (*edge_cursor)++;
    return 1;
}

THARAVU_API int THARAVU_CALL tgph_out_degree(const ogph_graph_t *g, int node_idx)
{
    if (!g || node_idx < 0 || node_idx >= (int)g->node_count) return 0;
    return (int)(g->row_ptr[node_idx + 1] - g->row_ptr[node_idx]);
}
