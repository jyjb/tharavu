/*
 * tharavu_dll.h  —  Public DLL/SO API for the Tharavu Data Engine
 *
 * All external programs (C, C#, Python, Java, …) include ONLY this header.
 * The internal data_engine.h is never exposed to consumers.
 *
 * Functions are prefixed `tde_` ("Tharavu Data Engine") to avoid clashing
 * with the internal `de_*` symbol namespace.
 *
 * ── Calling convention ────────────────────────────────────────────────────
 *   THARAVU_CALL  = __cdecl on Windows, default on POSIX.
 *   Use this when declaring function pointers or P/Invoke signatures.
 *
 * ── C# example ────────────────────────────────────────────────────────────
 *   [DllImport("tharavu.dll", CallingConvention = CallingConvention.Cdecl)]
 *   static extern IntPtr tde_open([MarshalAs(UnmanagedType.LPStr)] string path);
 *
 * ── Python example ─────────────────────────────────────────────────────────
 *   lib = ctypes.CDLL("tharavu.dll")
 *   lib.tde_open.restype  = ctypes.c_void_p
 *   lib.tde_open.argtypes = [ctypes.c_char_p]
 *
 * ── Java (JNA) example ────────────────────────────────────────────────────
 *   public interface TharavuLib extends Library {
 *       Pointer tde_open(String path);
 *   }
 */

#ifndef THARAVU_DLL_H
#define THARAVU_DLL_H

#include <stdint.h>
#include <stddef.h>

/* ── Export / calling-convention macros ──────────────────────────────────── */

#ifdef _WIN32
#  ifdef THARAVU_EXPORTS          /* defined when building the DLL itself */
#    define THARAVU_API __declspec(dllexport)
#  else                           /* defined when consuming the DLL       */
#    define THARAVU_API __declspec(dllimport)
#  endif
#  define THARAVU_CALL __cdecl
#else
#  define THARAVU_API __attribute__((visibility("default")))
#  define THARAVU_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ── Opaque handle ───────────────────────────────────────────────────────── */

/* tde_handle_t is returned by open/create calls and passed back to every
 * subsequent operation.  Treat it as an opaque pointer — never dereference.
 * NULL is returned on failure; check tde_last_error() for the reason.       */
typedef void *tde_handle_t;

/* ── Re-exported constants (consumers need these without data_engine.h) ─── */

/* Cell types — matches DE_TYPE_* in tharavu_types.h */
#define TDE_TYPE_NULL   0
#define TDE_TYPE_INT32  1
#define TDE_TYPE_INT64  2
#define TDE_TYPE_FLOAT  3
#define TDE_TYPE_STRING 4
#define TDE_TYPE_BLOB   5

/* File types */
#define TDE_FILE_ODAT   1   /* Tabular row/column data          */
#define TDE_FILE_OVOC   2   /* Vocabulary hash table (.ovoc)    */
#define TDE_FILE_OVEC   3   /* Embedding vectors (.ovec)        */
#define TDE_FILE_OGPH   4   /* CSR graph (.ogph)                */

/* ── OGPH graph file constants ───────────────────────────────────────────── */

/* Header flags */
#define OGPH_FLAG_DIRECTED  0x0001
#define OGPH_FLAG_WEIGHTED  0x0002
#define OGPH_FLAG_TYPED     0x0004
#define OGPH_FLAG_EMBEDDED  0x0008

/* Node type IDs — cognitive primitives */
#define OGPH_NODE_OBJECT        0x01
#define OGPH_NODE_ACTOR         0x02
#define OGPH_NODE_ACTION        0x03
#define OGPH_NODE_STATE         0x04
#define OGPH_NODE_GOAL          0x05
#define OGPH_NODE_DEPENDENCY    0x06
#define OGPH_NODE_CONSTRAINT    0x07
#define OGPH_NODE_RESOURCE      0x08
#define OGPH_NODE_RELATIONSHIP  0x09
#define OGPH_NODE_TRANSITION    0x0A
#define OGPH_NODE_CONTEXT       0x0B
#define OGPH_NODE_VALIDATION    0x0C
#define OGPH_NODE_FAILURE       0x0D
#define OGPH_NODE_RECOVERY      0x0E
#define OGPH_NODE_POLICY        0x0F
#define OGPH_NODE_CONTRADICTION 0x10
#define OGPH_NODE_CONCEPT       0x11  /* generic domain concept */

/* Edge type IDs — knowledge graph */
#define OGPH_EDGE_IS_A          0x0001
#define OGPH_EDGE_HAS           0x0002
#define OGPH_EDGE_REQUIRES      0x0003
#define OGPH_EDGE_PERFORMS      0x0004
#define OGPH_EDGE_PRODUCES      0x0005
#define OGPH_EDGE_RELATED_TO    0x0006
#define OGPH_EDGE_CONSTRAINS    0x0007
/* Edge type IDs — semantic graph */
#define OGPH_EDGE_DEPENDS_ON     0x0010
#define OGPH_EDGE_TRANSITIONS_TO 0x0011
#define OGPH_EDGE_CAUSAL         0x0012
#define OGPH_EDGE_CONTRADICTS    0x0013
#define OGPH_EDGE_RECOVERS_VIA   0x0014

/* Edge flags */
#define OGPH_EDGE_FLAG_LOW_CONFIDENCE 0x0001

/* Error codes — matches DE_ERR_* in tharavu_types.h */
#define TDE_OK           0
#define TDE_ERR_IO      -1
#define TDE_ERR_CORRUPT -2
#define TDE_ERR_MEM     -3
#define TDE_ERR_LOCK    -4
#define TDE_ERR_NOTFOUND -5
#define TDE_ERR_INVAL   -6

/* ══════════════════════════════════════════════════════════════════════════
 *  API
 * ══════════════════════════════════════════════════════════════════════════ */

/* ── Version ─────────────────────────────────────────────────────────────── */

/* Compile-time ABI version — increment whenever a function signature changes,
 * a symbol is removed, or a struct layout visible to consumers changes.
 * Consumers can guard against mismatches:
 *   #if THARAVU_ABI_VERSION != 1
 *   #  error "tharavu.dll ABI mismatch — update the header"
 *   #endif
 * and verify at startup:  assert(tde_abi_version() == THARAVU_ABI_VERSION); */
#define THARAVU_ABI_VERSION 2

THARAVU_API int THARAVU_CALL tde_version_major(void);
THARAVU_API int THARAVU_CALL tde_version_minor(void);

/* Returns THARAVU_ABI_VERSION as compiled into the DLL.
 * Compare against the header constant to detect consumer/DLL mismatches. */
THARAVU_API int THARAVU_CALL tde_abi_version(void);

/* ── Global configuration ────────────────────────────────────────────────── */

/* Set the root directory for logical-name resolution ("db.table" → path).
 * Default: "./data".  Call before any tde_open_* / tde_build_* functions.  */
THARAVU_API void THARAVU_CALL tde_set_base_path(const char *path);

/* Return the current base path set by tde_set_base_path().
 * Returns a pointer to internal storage — valid until the next tde_set_base_path() call. */
THARAVU_API const char *THARAVU_CALL tde_get_base_path(void);

/* Set the embedding vector dimension.
 * Must be called before any OVEC or OVOC operations.
 * Default: 64 */
THARAVU_API void THARAVU_CALL tde_set_dim(uint32_t dim);

/* Set the vocabulary hash table capacity.
 * Must be called before any OVOC operations.
 * Default: 131072 */
THARAVU_API void THARAVU_CALL tde_set_hash_cap(uint32_t cap);

/*
 * tde_config_load() REMOVED 2026-05-29
 * Config reading moved to caller via SLispManager + slm_load().
 *
 * Migration:
 *   slm_node_t *cfg = slm_load("thiraviyan.ocfg");
 *   slm_node_t *t   = slm_find(cfg, "tharavu");
 *   char base[512];
 *   slm_resolve_path(cfg, slm_attr(t, "base_path"), base, sizeof(base));
 *   tde_set_base_path(base);
 *   tde_set_dim((uint32_t)slm_attr_i(t, "dim", 64));
 *   tde_set_hash_cap((uint32_t)slm_attr_i(t, "hash_cap", 131072));
 *   slm_free(cfg);
 */

/* ── Table lifecycle ─────────────────────────────────────────────────────── */

/* Open an existing data file by absolute/relative path.
 * The file type (ODAT / OVOC / OVEC) is detected from the magic bytes.
 * Returns NULL on error; call tde_last_error() for the code.               */
THARAVU_API tde_handle_t THARAVU_CALL tde_open(const char *filepath);

/* Open using a logical name "dbname.tablename" for each file type.         */
THARAVU_API tde_handle_t THARAVU_CALL tde_open_odat(const char *logical_name);
THARAVU_API tde_handle_t THARAVU_CALL tde_open_ovoc(const char *logical_name);
THARAVU_API tde_handle_t THARAVU_CALL tde_open_ovec(const char *logical_name);

/* Create a new empty ODAT table in memory (not yet on disk).
 * col_names: array of col_count null-terminated strings.
 * Returns NULL on error.                                                    */
THARAVU_API tde_handle_t THARAVU_CALL tde_create(const char **col_names, int col_count);

/* Persist an ODAT table to disk atomically (write-then-rename).
 * Returns TDE_OK or a negative error code.                                 */
THARAVU_API int THARAVU_CALL tde_save(tde_handle_t h, const char *filepath);

/* Save using a logical name — resolves path automatically.                 */
THARAVU_API int THARAVU_CALL tde_save_logical(tde_handle_t h, const char *logical_name);

/* Release all resources held by the handle.  Always call this when done.  */
THARAVU_API void THARAVU_CALL tde_close(tde_handle_t h);

/* ── Schema / metadata ───────────────────────────────────────────────────── */

THARAVU_API int         THARAVU_CALL tde_row_count(tde_handle_t h);
THARAVU_API int         THARAVU_CALL tde_col_count(tde_handle_t h);
THARAVU_API int         THARAVU_CALL tde_file_type(tde_handle_t h);  /* TDE_FILE_* */

/* Returns the column name string.  Pointer valid until tde_close(h).
 * Returns NULL if col is out of range.                                      */
THARAVU_API const char *THARAVU_CALL tde_col_name(tde_handle_t h, int col);

/* ── Cell read accessors ─────────────────────────────────────────────────── */

/* Returns TDE_TYPE_* for the cell, or TDE_ERR_INVAL for bad row/col.      */
THARAVU_API int THARAVU_CALL tde_get_cell_type(tde_handle_t h, int row, int col);

/* Typed getters — return TDE_OK or a negative error code.                  */
THARAVU_API int THARAVU_CALL tde_get_int32  (tde_handle_t h, int row, int col, int32_t *out);
THARAVU_API int THARAVU_CALL tde_get_float32(tde_handle_t h, int row, int col, float   *out);

/* String getter — two-call pattern:
 *   1. Pass buf=NULL → returns required buffer size including the NUL byte.
 *   2. Pass buf with sufficient size → copies string, returns bytes written.
 * Returns a negative error code on invalid row/col/type.                   */
THARAVU_API int THARAVU_CALL tde_get_string(tde_handle_t h, int row, int col,
                                             char *buf, int buf_size);

/* ── Row builder (ODAT insert) ──────────────────────────────────────────── */

/* Begin building a new row for the given table.
 * Returns a "row handle" that must be committed or discarded.
 * Returns NULL on error.                                                    */
THARAVU_API tde_handle_t THARAVU_CALL tde_row_begin(tde_handle_t table_h);

/* Set individual cells in the pending row.  col is 0-based.               */
THARAVU_API int THARAVU_CALL tde_row_set_int32  (tde_handle_t row_h, int col, int32_t    val);
THARAVU_API int THARAVU_CALL tde_row_set_float32(tde_handle_t row_h, int col, float      val);
THARAVU_API int THARAVU_CALL tde_row_set_string (tde_handle_t row_h, int col, const char *val);

/* Commit: inserts the row into the table and invalidates row_h.
 * Returns TDE_OK or a negative error code.                                 */
THARAVU_API int  THARAVU_CALL tde_row_commit(tde_handle_t row_h);

/* Discard: cancels the pending row without inserting; invalidates row_h.  */
THARAVU_API void THARAVU_CALL tde_row_discard(tde_handle_t row_h);

/* ── Cell update / row delete (ODAT, in-memory) ─────────────────────────── */

THARAVU_API int THARAVU_CALL tde_update_int32  (tde_handle_t h, int row, int col, int32_t    val);
THARAVU_API int THARAVU_CALL tde_update_float32(tde_handle_t h, int row, int col, float      val);
THARAVU_API int THARAVU_CALL tde_update_string (tde_handle_t h, int row, int col, const char *val);
THARAVU_API int THARAVU_CALL tde_delete_row    (tde_handle_t h, int row);

/* ── Query ───────────────────────────────────────────────────────────────── */

/* Returns a new independent table containing all rows where
 * column == value (string comparison; integers are matched by value).
 * The result is owned by the caller — ALWAYS call tde_close() when done,
 * even if the result is empty or contains few rows. Forgetting to close
 * leaks file handles, especially problematic for .ovec and .ovoc mmap'd files.
 * Returns NULL if no rows match or on error; check tde_last_error().       */
THARAVU_API tde_handle_t THARAVU_CALL tde_find(tde_handle_t h,
                                                const char *column,
                                                const char *value);

/* Zero-alloc row-index query.  Writes matching row indices into ids_out.
 * max_results: capacity of ids_out.
 * Returns the total number of matches found (may exceed max_results —
 * caller detects truncation when return value > max_results).
 * No allocation, no deep copy — safe for hot loops.                        */
THARAVU_API int THARAVU_CALL tde_find_ids(tde_handle_t  h,
                                           const char   *column,
                                           const char   *value,
                                           int          *ids_out,
                                           int           max_results);

/* ── Vocabulary (.ovoc) ─────────────────────────────────────────────────── */

/* Sentinel written into tokens_out for words that are not found.           */
#define TDE_TOKEN_UNKNOWN UINT32_MAX

/* O(1) single word → token lookup.  Returns TDE_OK and sets *out_id.      */
THARAVU_API int THARAVU_CALL tde_vocab_lookup(tde_handle_t h,
                                               const char *word,
                                               uint32_t   *out_id);

/* Bulk word → token.  words[i] → tokens_out[i]; missing = TDE_TOKEN_UNKNOWN.
 * Returns number resolved, or negative error code.
 * No allocation inside the call — safe for tight loops.                    */
THARAVU_API int THARAVU_CALL tde_vocab_lookup_batch(tde_handle_t    h,
                                                     const char    **words,
                                                     int             count,
                                                     uint32_t       *tokens_out);

/* O(1) token → word (zero-copy pointer into mmap).
 * Pointer valid until tde_close(h).  Returns NULL if token_id is out of
 * range or the file was built without a reverse index (pre-v1.1).
 * out_len (optional): receives the word's byte length (no NUL).            */
THARAVU_API const char *THARAVU_CALL tde_vocab_reverse_lookup(tde_handle_t h,
                                                               uint32_t     token_id,
                                                               uint16_t    *out_len);

/* Extended reverse lookup: also returns per-token flags and a zero-copy
 * pointer to the embedded float vector (NULL when dim == 0).
 * out_len, out_flags, out_vec are all optional (may be NULL).
 * out_flags receives the full 64-bit domain+intent bitmask.                */
THARAVU_API const char *THARAVU_CALL tde_vocab_reverse_lookup_ex(tde_handle_t   h,
                                                                   uint32_t       token_id,
                                                                   uint16_t      *out_len,
                                                                   uint64_t      *out_flags,
                                                                   const float  **out_vec);

/* Bulk token → word (zero-copy).  words_out[i] = mmap pointer or NULL.
 * Returns number resolved, or negative error code.                         */
THARAVU_API int THARAVU_CALL tde_vocab_reverse_batch(tde_handle_t     h,
                                                      const uint32_t  *token_ids,
                                                      int              count,
                                                      const char     **words_out);

/* ── Vectors (.ovec) ────────────────────────────────────────────────────── */

/* Copy the float vector for row_id into buf (caller-allocated).
 * buf_floats: capacity of buf in number of floats.
 * out_dim: if non-NULL, receives the actual dimension of the vector.
 * Returns the number of floats written on success, or a negative error.
 * Returns TDE_ERR_MEM if buf_floats < dim.                                 */
THARAVU_API int THARAVU_CALL tde_vector_get_row(tde_handle_t h,
                                                 uint32_t     row_id,
                                                 float       *buf,
                                                 uint32_t     buf_floats,
                                                 uint32_t    *out_dim);

/* Zero-copy pointer directly into the mmap region.
 * Valid until tde_close(h).  Returns NULL on out-of-range row_id.
 * out_dim (optional): receives the vector dimension.                       */
THARAVU_API const float *THARAVU_CALL tde_vector_get_ptr(tde_handle_t h,
                                                          uint32_t     row_id,
                                                          uint32_t    *out_dim);

/* Bulk vector fetch.  row_ids[i] → out_buf[i*dim .. i*dim+dim-1].
 * out_buf must hold count * dim floats (caller allocates).
 * Missing row IDs fill the corresponding slice with 0.0f.
 * Returns count on success, or a negative error code.                      */
THARAVU_API int THARAVU_CALL tde_vector_get_batch(tde_handle_t    h,
                                                   const uint32_t *row_ids,
                                                   int             count,
                                                   float          *out_buf,
                                                   uint32_t       *out_dim);

/* ── Top-k vector search ─────────────────────────────────────────────────── */

/* Brute-force cosine-similarity top-k search over the entire .ovec store.
 * query_vec:  caller-allocated float[dim] query embedding.
 * dim:        must match the .ovec file's stored dimension.
 * k:          maximum number of results to return.
 * ids_out:    caller-allocated uint32_t[k] — receives result row IDs.
 * scores_out: caller-allocated float[k]    — receives cosine scores in [-1, 1].
 * Results are written sorted best-first (highest cosine similarity first).
 * Returns the actual number of results written (≤k), or a negative error code. */
THARAVU_API int THARAVU_CALL tde_vector_search_topk(tde_handle_t  h,
                                                     const float  *query_vec,
                                                     uint32_t      dim,
                                                     uint32_t      k,
                                                     uint32_t     *ids_out,
                                                     float        *scores_out);

/* ── File builders ───────────────────────────────────────────────────────── */

/* Build a vocabulary (.ovoc) file from an array of words.
 * words[0..count-1] are null-terminated strings; token IDs == array index.
 * vectors: flat row-major float[count * dim] embedding per token, or NULL.
 * dim: embedding dimension; 0 means no embedded vectors.
 * flags: per-token uint64 domain+intent bitmask array, or NULL (all flags=0).
 *        Written as an 8-byte field per record (ABI v2).
 * Returns TDE_OK or a negative error code.                                 */
THARAVU_API int THARAVU_CALL tde_build_vocab(const char     *filepath,
                                              const char    **words,
                                              int             count,
                                              const float    *vectors,
                                              uint32_t        dim,
                                              const uint64_t *flags);

THARAVU_API int THARAVU_CALL tde_build_vocab_logical(const char     *logical_name,
                                                      const char    **words,
                                                      int             count,
                                                      const float    *vectors,
                                                      uint32_t        dim,
                                                      const uint64_t *flags);

/* Build a vector file (.ovec) from a FLAT row-major float array.
 * data points to count * dim floats laid out as:
 *   [vec0_f0, vec0_f1, ..., vec0_f(dim-1), vec1_f0, ...]
 * Returns TDE_OK or a negative error code.                                 */
THARAVU_API int THARAVU_CALL tde_build_vectors(const char  *filepath,
                                                const float *data,
                                                int          count,
                                                uint32_t     dim);

THARAVU_API int THARAVU_CALL tde_build_vectors_logical(const char  *logical_name,
                                                        const float *data,
                                                        int          count,
                                                        uint32_t     dim);

/* ── Data Export ─────────────────────────────────────────────────────────── */

/* Export table data to CSV format.
 * Returns TDE_OK on success, or a negative error code.
 * The CSV file will have column headers in the first row.                */
THARAVU_API int THARAVU_CALL tde_export_csv(tde_handle_t h, const char *csv_filepath);

/* ── Error handling ──────────────────────────────────────────────────────── */

/* Human-readable description of an error code.                             */
THARAVU_API const char *THARAVU_CALL tde_strerror(int code);

/* Last error code recorded on the calling thread.
 * Reset to TDE_OK at the start of every API call.                          */
THARAVU_API int THARAVU_CALL tde_last_error(void);

/* ── Graph API (ogph.h / ogph_builder.h) ─────────────────────────────────── */
/* Callers get the full tgph_* graph API from a single tharavu_dll.h include. */
#include "ogph.h"
#include "ogph_builder.h"

#ifdef __cplusplus
}
#endif

#endif /* THARAVU_DLL_H */
