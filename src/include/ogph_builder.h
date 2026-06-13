/*
 * ogph_builder.h  —  Internal OGPH graph builder API (not installed)
 *
 * Arena-based builder that accumulates nodes and edges in memory,
 * then writes a well-formed .ogph file atomically on finalise.
 * Include via tharavu_dll.h — do not include directly.
 */

#ifndef OGPH_BUILDER_H
#define OGPH_BUILDER_H

#include <stdint.h>
#include "ogph.h"

typedef struct ogph_builder_s ogph_builder_t;

/* Create builder.
 * node_capacity / edge_capacity: initial allocation hints, not hard limits.
 * embed_dim: 0 = no embeddings; > 0 enables embedding block.
 * flags: OGPH_FLAG_* bitmask. */
THARAVU_API ogph_builder_t * THARAVU_CALL tgph_builder_create(int node_capacity,
                                                               int edge_capacity,
                                                               int embed_dim,
                                                               uint16_t flags);

/* Add node — returns node index >= 0 on success, -1 on error.
 * embed may be NULL (embed_off stored as 0xFFFFFFFF).
 * label may be NULL (stored as empty string). */
THARAVU_API int THARAVU_CALL tgph_builder_add_node(ogph_builder_t *b,
                                                    uint64_t    node_id,
                                                    uint32_t    type_id,
                                                    const char *label,
                                                    const float *embed,
                                                    uint32_t    node_flags);

/* Add directed edge — src_idx / tgt_idx must be valid indices from add_node.
 * Returns 0 on success, -1 on error. */
THARAVU_API int THARAVU_CALL tgph_builder_add_edge(ogph_builder_t *b,
                                                    int      src_idx,
                                                    int      tgt_idx,
                                                    uint16_t type_id,
                                                    float    weight,
                                                    uint16_t edge_flags);

/* Finalise: sort into CSR order, write .ogph atomically.
 * Returns TDE_OK (0) on success or a negative TDE_ERR_* code. */
THARAVU_API int THARAVU_CALL tgph_builder_finalise        (ogph_builder_t *b, const char *path);
THARAVU_API int THARAVU_CALL tgph_builder_finalise_logical(ogph_builder_t *b, const char *logical_name);

/* Free all builder resources. Always call after finalise or on error. */
THARAVU_API void THARAVU_CALL tgph_builder_free(ogph_builder_t *b);

#endif /* OGPH_BUILDER_H */
