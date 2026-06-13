/*
 * ogph_builder.c  —  OGPH graph file builder
 *
 * Implements tgph_builder_* API declared in ogph_builder.h.
 * Accumulates nodes and edges in arena-style growing arrays,
 * then on finalise: builds CSR row pointers, serialises the
 * .ogph layout, and atomically renames the temp file.
 */

#include "include/ogph_builder.h"
#include "include/ogph.h"
#include "include/data_engine.h"
#include "include/tharavu_dll.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#  include <windows.h>
#  define ogphb_unlink _unlink
#else
#  include <unistd.h>
#  define ogphb_unlink unlink
#endif

/* ── On-disk structures (must match ogph.c) ──────────────────────────────── */

#pragma pack(push, 1)

typedef struct {
    char     magic[4];
    uint16_t abi_version;
    uint16_t flags;
    uint32_t node_count;
    uint32_t edge_count;
    uint32_t node_prop_dim;
    uint32_t string_pool_size;
    char     reserved[40];
} ogphb_header_t;   /* 64 bytes */

typedef struct {
    uint64_t node_id;
    uint32_t type_id;
    uint32_t label_off;
    uint32_t embed_off;
    uint32_t edge_start;
    uint32_t node_flags;
    uint32_t _pad;
} ogphb_node_rec_t;  /* 32 bytes */

typedef struct {
    uint32_t target_node;
    uint16_t type_id;
    uint16_t edge_flags;
    float    weight;
    uint32_t reserved;
} ogphb_edge_rec_t;  /* 16 bytes */

#pragma pack(pop)

/* ── Builder internal node / edge entries ────────────────────────────────── */

typedef struct {
    uint64_t  node_id;
    uint32_t  type_id;
    uint32_t  node_flags;
    char     *label;   /* heap-allocated copy; NULL → "" stored */
    float    *embed;   /* heap-allocated copy; NULL → no embed  */
} bnode_t;

typedef struct {
    int      src_idx;
    int      tgt_idx;
    uint16_t type_id;
    uint16_t edge_flags;
    float    weight;
} bedge_t;

struct ogph_builder_s {
    bnode_t  *nodes;
    int       node_count;
    int       node_cap;

    bedge_t  *edges;
    int       edge_count;
    int       edge_cap;

    int       embed_dim;
    uint16_t  flags;
};

/* ── tgph_builder_create ─────────────────────────────────────────────────── */

THARAVU_API ogph_builder_t * THARAVU_CALL tgph_builder_create(int node_capacity,
                                                               int edge_capacity,
                                                               int embed_dim,
                                                               uint16_t flags)
{
    if (node_capacity < 1) node_capacity = 16;
    if (edge_capacity < 1) edge_capacity = 16;

    ogph_builder_t *b = (ogph_builder_t *)calloc(1, sizeof(*b));
    if (!b) return NULL;

    b->nodes = (bnode_t *)malloc((size_t)node_capacity * sizeof(bnode_t));
    b->edges = (bedge_t *)malloc((size_t)edge_capacity * sizeof(bedge_t));
    if (!b->nodes || !b->edges) {
        free(b->nodes);
        free(b->edges);
        free(b);
        return NULL;
    }

    b->node_cap  = node_capacity;
    b->edge_cap  = edge_capacity;
    b->embed_dim = (embed_dim > 0) ? embed_dim : 0;
    b->flags     = flags;
    return b;
}

/* ── tgph_builder_add_node ───────────────────────────────────────────────── */

THARAVU_API int THARAVU_CALL tgph_builder_add_node(ogph_builder_t *b,
                                                    uint64_t    node_id,
                                                    uint32_t    type_id,
                                                    const char *label,
                                                    const float *embed,
                                                    uint32_t    node_flags)
{
    if (!b) return -1;

    /* Grow nodes array if needed */
    if (b->node_count >= b->node_cap) {
        int new_cap = b->node_cap * 2;
        bnode_t *tmp = (bnode_t *)realloc(b->nodes, (size_t)new_cap * sizeof(bnode_t));
        if (!tmp) return -1;
        b->nodes    = tmp;
        b->node_cap = new_cap;
    }

    bnode_t *n = &b->nodes[b->node_count];
    n->node_id   = node_id;
    n->type_id   = type_id;
    n->node_flags = node_flags;

    n->label = label ? strdup(label) : strdup("");
    if (!n->label) return -1;

    n->embed = NULL;
    if (embed && b->embed_dim > 0) {
        n->embed = (float *)malloc((size_t)b->embed_dim * sizeof(float));
        if (!n->embed) { free(n->label); n->label = NULL; return -1; }
        memcpy(n->embed, embed, (size_t)b->embed_dim * sizeof(float));
    }

    return b->node_count++;
}

/* ── tgph_builder_add_edge ───────────────────────────────────────────────── */

THARAVU_API int THARAVU_CALL tgph_builder_add_edge(ogph_builder_t *b,
                                                    int      src_idx,
                                                    int      tgt_idx,
                                                    uint16_t type_id,
                                                    float    weight,
                                                    uint16_t edge_flags)
{
    if (!b) return -1;
    if (src_idx < 0 || src_idx >= b->node_count) return -1;
    if (tgt_idx < 0 || tgt_idx >= b->node_count) return -1;

    if (b->edge_count >= b->edge_cap) {
        int new_cap = b->edge_cap * 2;
        bedge_t *tmp = (bedge_t *)realloc(b->edges, (size_t)new_cap * sizeof(bedge_t));
        if (!tmp) return -1;
        b->edges    = tmp;
        b->edge_cap = new_cap;
    }

    bedge_t *e = &b->edges[b->edge_count++];
    e->src_idx    = src_idx;
    e->tgt_idx    = tgt_idx;
    e->type_id    = type_id;
    e->edge_flags = edge_flags;
    e->weight     = weight;
    return 0;
}

/* ── tgph_builder_free ───────────────────────────────────────────────────── */

THARAVU_API void THARAVU_CALL tgph_builder_free(ogph_builder_t *b)
{
    if (!b) return;
    for (int i = 0; i < b->node_count; i++) {
        free(b->nodes[i].label);
        free(b->nodes[i].embed);
    }
    free(b->nodes);
    free(b->edges);
    free(b);
}

/* ── tgph_builder_finalise ───────────────────────────────────────────────── */

THARAVU_API int THARAVU_CALL tgph_builder_finalise(ogph_builder_t *b,
                                                    const char *path)
{
    if (!b || !path) return TDE_ERR_INVAL;

    int nc  = b->node_count;
    int ec  = b->edge_count;
    int dim = b->embed_dim;

    /* ── 1. Build string pool ─────────────────────────────────────────────── */

    /* First pass: compute total pool size */
    uint32_t pool_size = 0;
    for (int i = 0; i < nc; i++) {
        const char *lbl = b->nodes[i].label ? b->nodes[i].label : "";
        pool_size += (uint32_t)(strlen(lbl) + 1u);
    }

    char *pool = (char *)malloc(pool_size > 0 ? pool_size : 1);
    if (!pool) return TDE_ERR_MEM;

    uint32_t *label_offs = (uint32_t *)malloc((size_t)(nc > 0 ? nc : 1) * sizeof(uint32_t));
    if (!label_offs) { free(pool); return TDE_ERR_MEM; }

    uint32_t pool_off = 0;
    for (int i = 0; i < nc; i++) {
        const char *lbl = b->nodes[i].label ? b->nodes[i].label : "";
        size_t llen = strlen(lbl) + 1u;
        memcpy(pool + pool_off, lbl, llen);
        label_offs[i] = pool_off;
        pool_off += (uint32_t)llen;
    }

    /* ── 2. Build embed offsets ───────────────────────────────────────────── */

    uint32_t *embed_offs = (uint32_t *)malloc((size_t)(nc > 0 ? nc : 1) * sizeof(uint32_t));
    if (!embed_offs) { free(pool); free(label_offs); return TDE_ERR_MEM; }

    uint32_t embed_block_bytes = 0;
    for (int i = 0; i < nc; i++) {
        if (dim > 0 && b->nodes[i].embed) {
            embed_offs[i]      = embed_block_bytes;
            embed_block_bytes += (uint32_t)dim * (uint32_t)sizeof(float);
        } else {
            embed_offs[i] = 0xFFFFFFFFu;
        }
    }

    /* ── 3. Build CSR row pointers ────────────────────────────────────────── */

    uint32_t *row_ptr = (uint32_t *)calloc((size_t)(nc + 1), sizeof(uint32_t));
    if (!row_ptr) { free(pool); free(label_offs); free(embed_offs); return TDE_ERR_MEM; }

    /* Count edges per source */
    for (int i = 0; i < ec; i++)
        row_ptr[b->edges[i].src_idx + 1]++;

    /* Prefix sum */
    for (int i = 1; i <= nc; i++)
        row_ptr[i] += row_ptr[i - 1];

    /* Fill edge array using a cursor copy of row_ptr */
    ogphb_edge_rec_t *edge_arr = (ogphb_edge_rec_t *)calloc(
        (size_t)(ec > 0 ? ec : 1), sizeof(ogphb_edge_rec_t));
    if (!edge_arr) {
        free(pool); free(label_offs); free(embed_offs); free(row_ptr);
        return TDE_ERR_MEM;
    }

    uint32_t *cursor = (uint32_t *)malloc((size_t)(nc + 1) * sizeof(uint32_t));
    if (!cursor) {
        free(pool); free(label_offs); free(embed_offs); free(row_ptr); free(edge_arr);
        return TDE_ERR_MEM;
    }
    memcpy(cursor, row_ptr, (size_t)(nc + 1) * sizeof(uint32_t));

    for (int i = 0; i < ec; i++) {
        const bedge_t *be   = &b->edges[i];
        uint32_t       pos  = cursor[be->src_idx]++;
        ogphb_edge_rec_t *e = &edge_arr[pos];
        e->target_node = (uint32_t)be->tgt_idx;
        e->type_id     = be->type_id;
        e->edge_flags  = be->edge_flags;
        e->weight      = be->weight;
        e->reserved    = 0;
    }
    free(cursor);

    /* ── 4. Build node table ──────────────────────────────────────────────── */

    ogphb_node_rec_t *node_arr = (ogphb_node_rec_t *)calloc(
        (size_t)(nc > 0 ? nc : 1), sizeof(ogphb_node_rec_t));
    if (!node_arr) {
        free(pool); free(label_offs); free(embed_offs); free(row_ptr); free(edge_arr);
        return TDE_ERR_MEM;
    }

    for (int i = 0; i < nc; i++) {
        ogphb_node_rec_t *nr = &node_arr[i];
        nr->node_id    = b->nodes[i].node_id;
        nr->type_id    = b->nodes[i].type_id;
        nr->label_off  = label_offs[i];
        nr->embed_off  = embed_offs[i];
        nr->edge_start = row_ptr[i];
        nr->node_flags = b->nodes[i].node_flags;
        nr->_pad       = 0;
    }
    free(label_offs);
    free(embed_offs);

    /* ── 5. Build header ──────────────────────────────────────────────────── */

    ogphb_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, "OGPH", 4);
    hdr.abi_version     = 1;
    hdr.flags           = b->flags;
    hdr.node_count      = (uint32_t)nc;
    hdr.edge_count      = (uint32_t)ec;
    hdr.node_prop_dim   = (uint32_t)dim;
    hdr.string_pool_size = pool_size;

    /* ── 6. Write to temp file, atomic rename ─────────────────────────────── */

    char temp[MAX_PATH_LEN];
    if (snprintf(temp, sizeof(temp), "%s.tmp", path) >= (int)sizeof(temp)) {
        free(pool); free(row_ptr); free(edge_arr); free(node_arr);
        return TDE_ERR_INVAL;
    }

    FILE *fp = fopen(temp, "wb");
    if (!fp) {
        free(pool); free(row_ptr); free(edge_arr); free(node_arr);
        return TDE_ERR_IO;
    }

#define WRITE_OR_FAIL(ptr, size, count) \
    if (fwrite((ptr), (size), (count), fp) != (count)) goto fail

    WRITE_OR_FAIL(&hdr,     sizeof(hdr),              1);
    WRITE_OR_FAIL(node_arr, sizeof(ogphb_node_rec_t), (size_t)nc);
    WRITE_OR_FAIL(row_ptr,  sizeof(uint32_t),          (size_t)(nc + 1));
    WRITE_OR_FAIL(edge_arr, sizeof(ogphb_edge_rec_t), (size_t)ec);
    if (pool_size > 0)
        WRITE_OR_FAIL(pool, 1, pool_size);

    /* Embedding block: iterate over nodes in order */
    if (dim > 0 && embed_block_bytes > 0) {
        for (int i = 0; i < nc; i++) {
            if (b->nodes[i].embed) {
                WRITE_OR_FAIL(b->nodes[i].embed, sizeof(float), (size_t)dim);
            }
        }
    }

#undef WRITE_OR_FAIL

    fclose(fp);
    fp = NULL;

    free(pool);
    free(row_ptr);
    free(edge_arr);
    free(node_arr);

#ifdef _WIN32
    if (!de_platform_atomic_rename(temp, path)) {
        ogphb_unlink(temp);
        return TDE_ERR_IO;
    }
#else
    if (rename(temp, path) != 0) {
        ogphb_unlink(temp);
        return TDE_ERR_IO;
    }
#endif
    return TDE_OK;

fail:
    fclose(fp);
    ogphb_unlink(temp);
    free(pool);
    free(row_ptr);
    free(edge_arr);
    free(node_arr);
    return TDE_ERR_IO;
}

THARAVU_API int THARAVU_CALL tgph_builder_finalise_logical(ogph_builder_t *b,
                                                            const char *logical_name)
{
    if (!b || !logical_name) return TDE_ERR_INVAL;
    char path[MAX_PATH_LEN];
    if (de_resolve_path(logical_name, 4, path, sizeof(path)) != TDE_OK)
        return TDE_ERR_INVAL;
    return tgph_builder_finalise(b, path);
}
