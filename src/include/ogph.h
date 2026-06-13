/*
 * ogph.h  —  Internal OGPH graph file API (not installed)
 *
 * Opaque graph handle for reading .ogph CSR files.
 * Include via tharavu_dll.h — do not include directly.
 */

#ifndef OGPH_H
#define OGPH_H

#include <stdint.h>

/* Export / calling-convention macros (mirrors tharavu_dll.h — guards prevent
 * double-definition when included through tharavu_dll.h). */
#ifndef THARAVU_API
#  ifdef _WIN32
#    ifdef THARAVU_EXPORTS
#      define THARAVU_API __declspec(dllexport)
#    else
#      define THARAVU_API __declspec(dllimport)
#    endif
#    define THARAVU_CALL __cdecl
#  else
#    define THARAVU_API __attribute__((visibility("default")))
#    define THARAVU_CALL
#  endif
#endif

/* Opaque graph handle */
typedef struct ogph_graph_s ogph_graph_t;

/* ── Open / Close ─────────────────────────────────────────────────────────── */
THARAVU_API ogph_graph_t * THARAVU_CALL tgph_open        (const char *path);
THARAVU_API ogph_graph_t * THARAVU_CALL tgph_open_logical(const char *logical_name);
THARAVU_API int            THARAVU_CALL tgph_save        (const ogph_graph_t *g, const char *path);
THARAVU_API int            THARAVU_CALL tgph_save_logical(const ogph_graph_t *g, const char *logical_name);
THARAVU_API void           THARAVU_CALL tgph_close       (ogph_graph_t *g);

/* ── Graph metadata ───────────────────────────────────────────────────────── */
THARAVU_API int      THARAVU_CALL tgph_node_count      (const ogph_graph_t *g);
THARAVU_API int      THARAVU_CALL tgph_edge_count_total(const ogph_graph_t *g);
THARAVU_API uint16_t THARAVU_CALL tgph_flags           (const ogph_graph_t *g);
THARAVU_API int      THARAVU_CALL tgph_embed_dim       (const ogph_graph_t *g);

/* ── Node queries — O(1) via FNV-1a hash index ───────────────────────────── */
THARAVU_API int          THARAVU_CALL tgph_find_node  (const ogph_graph_t *g, uint64_t node_id);
THARAVU_API uint64_t     THARAVU_CALL tgph_node_id    (const ogph_graph_t *g, int idx);
THARAVU_API int          THARAVU_CALL tgph_node_type  (const ogph_graph_t *g, int idx);
THARAVU_API const char * THARAVU_CALL tgph_node_label (const ogph_graph_t *g, int idx);
THARAVU_API uint32_t     THARAVU_CALL tgph_node_flags (const ogph_graph_t *g, int idx);
THARAVU_API const float *THARAVU_CALL tgph_node_embed (const ogph_graph_t *g, int idx);

/* ── Edge traversal — CSR O(degree) ──────────────────────────────────────── */
THARAVU_API int THARAVU_CALL tgph_next_edge(const ogph_graph_t *g,
                                             int      node_idx,
                                             int     *edge_cursor,   /* in/out; init to 0 */
                                             int     *target_idx,    /* out */
                                             uint16_t *type_id,      /* out */
                                             float    *weight);      /* out */
THARAVU_API int THARAVU_CALL tgph_out_degree(const ogph_graph_t *g, int node_idx);

#endif /* OGPH_H */
