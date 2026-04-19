/*
 * tharavu_dll.c  —  DLL wrapper implementation
 *
 * Exports the tde_* public API (declared in tharavu_dll.h) as a shared
 * library.  Internally delegates to the de_* engine functions.
 *
 * Build:
 *   Windows (MinGW):  gcc -std=c99 -O2 -DTHARAVU_EXPORTS -shared \
 *                         -I./include src/data_engine.c src/platform.c \
 *                         src/tharavu_dll.c -o tharavu.dll \
 *                         -Wl,--out-implib,libtharavu_dll.a
 *   Linux/macOS:      gcc -std=c99 -O2 -DTHARAVU_EXPORTS -fPIC -shared \
 *                         -I./include src/data_engine.c src/platform.c \
 *                         src/tharavu_dll.c -o libtharavu.so
 */

#include "../include/tharavu_dll.h"   /* public API — defines THARAVU_EXPORTS */
#include "../include/data_engine.h"   /* internal engine API                  */
#include <stdlib.h>
#include <string.h>

/* ── Thread-local last error ─────────────────────────────────────────────── */

#if defined(_MSC_VER)
  static __declspec(thread) int g_last_err = TDE_OK;
#elif defined(__GNUC__) || defined(__clang__)
  static __thread int g_last_err = TDE_OK;
#else
  static _Thread_local int g_last_err = TDE_OK; /* C11 */
#endif

static int set_err(int code) { g_last_err = code; return code; }
static void *set_err_null(int code) { g_last_err = code; return NULL; }

/* ── Tagged handle structs ───────────────────────────────────────────────── */
/*
 * Both table handles and pending-row handles are returned as tde_handle_t
 * (void*).  Tags let us detect accidental misuse at runtime.
 */

#define MAGIC_TABLE 0x54424C55U  /* "TBLU" */
#define MAGIC_ROW   0x524F5755U  /* "ROWU" */

typedef struct {
    uint32_t magic;   /* MAGIC_TABLE */
    table_t  table;   /* embedded — de_free() zeroes this in place */
} table_handle_t;

typedef struct {
    uint32_t magic;          /* MAGIC_ROW  */
    table_handle_t *owner;   /* the table this row will be inserted into */
    cell_t         *cells;   /* array of owner->table.col_count cells    */
} row_handle_t;

/* ── Internal helpers ────────────────────────────────────────────────────── */

static table_handle_t *to_table(tde_handle_t h)
{
    if (!h) return NULL;
    table_handle_t *th = (table_handle_t *)h;
    return (th->magic == MAGIC_TABLE) ? th : NULL;
}

static row_handle_t *to_row(tde_handle_t h)
{
    if (!h) return NULL;
    row_handle_t *rh = (row_handle_t *)h;
    return (rh->magic == MAGIC_ROW) ? rh : NULL;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  API implementation
 * ══════════════════════════════════════════════════════════════════════════ */

/* ── Version ─────────────────────────────────────────────────────────────── */

THARAVU_API int THARAVU_CALL tde_version_major(void) { return VERSION_MAJOR; }
THARAVU_API int THARAVU_CALL tde_version_minor(void) { return VERSION_MINOR; }

/* ── Global configuration ────────────────────────────────────────────────── */

THARAVU_API void THARAVU_CALL tde_set_base_path(const char *path)
{
    de_set_base_path(path);
}

THARAVU_API int THARAVU_CALL tde_config_load(const char *ini_path)
{
    g_last_err = TDE_OK;
    tharavuConfig cfg;
    int res = de_config_load(ini_path, &cfg);
    if (res != DE_OK) return set_err(res);
    de_set_base_path(cfg.data_dir);
    return TDE_OK;
}

/* ── Table lifecycle ─────────────────────────────────────────────────────── */

THARAVU_API tde_handle_t THARAVU_CALL tde_open(const char *filepath)
{
    g_last_err = TDE_OK;
    table_handle_t *th = (table_handle_t *)calloc(1, sizeof(table_handle_t));
    if (!th) return set_err_null(TDE_ERR_MEM);

    int res = de_load(&th->table, filepath);
    if (res != DE_OK) { free(th); return set_err_null(res); }

    th->magic = MAGIC_TABLE;
    return (tde_handle_t)th;
}

THARAVU_API tde_handle_t THARAVU_CALL tde_open_odat(const char *logical_name)
{
    g_last_err = TDE_OK;
    table_handle_t *th = (table_handle_t *)calloc(1, sizeof(table_handle_t));
    if (!th) return set_err_null(TDE_ERR_MEM);

    int res = de_load_odat(logical_name, &th->table);
    if (res != DE_OK) { free(th); return set_err_null(res); }

    th->magic = MAGIC_TABLE;
    return (tde_handle_t)th;
}

THARAVU_API tde_handle_t THARAVU_CALL tde_open_ovoc(const char *logical_name)
{
    g_last_err = TDE_OK;
    table_handle_t *th = (table_handle_t *)calloc(1, sizeof(table_handle_t));
    if (!th) return set_err_null(TDE_ERR_MEM);

    int res = de_load_ovoc(logical_name, &th->table);
    if (res != DE_OK) { free(th); return set_err_null(res); }

    th->magic = MAGIC_TABLE;
    return (tde_handle_t)th;
}

THARAVU_API tde_handle_t THARAVU_CALL tde_open_ovec(const char *logical_name)
{
    g_last_err = TDE_OK;
    table_handle_t *th = (table_handle_t *)calloc(1, sizeof(table_handle_t));
    if (!th) return set_err_null(TDE_ERR_MEM);

    int res = de_load_ovec(logical_name, &th->table);
    if (res != DE_OK) { free(th); return set_err_null(res); }

    th->magic = MAGIC_TABLE;
    return (tde_handle_t)th;
}

THARAVU_API tde_handle_t THARAVU_CALL tde_create(const char **col_names, int col_count)
{
    g_last_err = TDE_OK;
    if (!col_names || col_count <= 0) return set_err_null(TDE_ERR_INVAL);

    /* de_create_table() heap-allocates a table_t; we need it inside our
     * tagged wrapper instead.  Allocate the wrapper and copy/transfer.     */
    table_t *tmp = de_create_table(col_names, col_count);
    if (!tmp) return set_err_null(TDE_ERR_MEM);

    table_handle_t *th = (table_handle_t *)calloc(1, sizeof(table_handle_t));
    if (!th) { de_destroy(tmp); return set_err_null(TDE_ERR_MEM); }

    th->magic = MAGIC_TABLE;
    th->table = *tmp;   /* shallow copy of the struct */
    free(tmp);          /* free the shell — data is now owned by th->table */
    return (tde_handle_t)th;
}

THARAVU_API int THARAVU_CALL tde_save(tde_handle_t h, const char *filepath)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th || !filepath) return set_err(TDE_ERR_INVAL);
    return set_err(de_save(&th->table, filepath));
}

THARAVU_API int THARAVU_CALL tde_save_logical(tde_handle_t h, const char *logical_name)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th || !logical_name) return set_err(TDE_ERR_INVAL);
    return set_err(de_save_logical(logical_name, &th->table));
}

THARAVU_API void THARAVU_CALL tde_close(tde_handle_t h)
{
    if (!h) return;
    table_handle_t *th = to_table(h);
    if (th) {
        de_free(&th->table);
        th->magic = 0; /* poison the handle */
        free(th);
        return;
    }
    /* Also accept a row handle that was not committed/discarded — clean up. */
    row_handle_t *rh = to_row(h);
    if (rh) { tde_row_discard(h); }
}

/* ── Schema / metadata ───────────────────────────────────────────────────── */

THARAVU_API int THARAVU_CALL tde_row_count(tde_handle_t h)
{
    table_handle_t *th = to_table(h);
    if (!th) { set_err(TDE_ERR_INVAL); return -1; }
    return th->table.row_count;
}

THARAVU_API int THARAVU_CALL tde_col_count(tde_handle_t h)
{
    table_handle_t *th = to_table(h);
    if (!th) { set_err(TDE_ERR_INVAL); return -1; }
    return th->table.col_count;
}

THARAVU_API int THARAVU_CALL tde_file_type(tde_handle_t h)
{
    table_handle_t *th = to_table(h);
    if (!th) { set_err(TDE_ERR_INVAL); return -1; }
    return th->table.file_type;
}

THARAVU_API const char *THARAVU_CALL tde_col_name(tde_handle_t h, int col)
{
    table_handle_t *th = to_table(h);
    if (!th || col < 0 || col >= th->table.col_count)
    {
        set_err(TDE_ERR_INVAL);
        return NULL;
    }
    return th->table.columns[col];
}

/* ── Cell read accessors ─────────────────────────────────────────────────── */

THARAVU_API int THARAVU_CALL tde_get_cell_type(tde_handle_t h, int row, int col)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th) return set_err(TDE_ERR_INVAL);

    cell_t cell;
    int res = de_get_cell(&th->table, row, col, &cell);
    if (res != DE_OK) return set_err(res);
    return cell.type;
}

THARAVU_API int THARAVU_CALL tde_get_int32(tde_handle_t h, int row, int col, int32_t *out)
{
    g_last_err = TDE_OK;
    if (!out) return set_err(TDE_ERR_INVAL);
    table_handle_t *th = to_table(h);
    if (!th) return set_err(TDE_ERR_INVAL);

    cell_t cell;
    int res = de_get_cell(&th->table, row, col, &cell);
    if (res != DE_OK) return set_err(res);
    if (cell.type != DE_TYPE_INT32) return set_err(TDE_ERR_INVAL);
    *out = cell.val.i32;
    return TDE_OK;
}

THARAVU_API int THARAVU_CALL tde_get_float32(tde_handle_t h, int row, int col, float *out)
{
    g_last_err = TDE_OK;
    if (!out) return set_err(TDE_ERR_INVAL);
    table_handle_t *th = to_table(h);
    if (!th) return set_err(TDE_ERR_INVAL);

    cell_t cell;
    int res = de_get_cell(&th->table, row, col, &cell);
    if (res != DE_OK) return set_err(res);
    if (cell.type != DE_TYPE_FLOAT) return set_err(TDE_ERR_INVAL);
    *out = cell.val.f32;
    return TDE_OK;
}

THARAVU_API int THARAVU_CALL tde_get_string(tde_handle_t h, int row, int col,
                                             char *buf, int buf_size)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th) return set_err(TDE_ERR_INVAL);

    cell_t cell;
    int res = de_get_cell(&th->table, row, col, &cell);
    if (res != DE_OK) return set_err(res);
    if (cell.type != DE_TYPE_STRING) return set_err(TDE_ERR_INVAL);

    const char *s = cell.val.str ? cell.val.str : "";
    int needed = (int)strlen(s) + 1; /* including NUL */

    if (!buf) return needed;         /* length-query call */

    if (buf_size < needed) return set_err(TDE_ERR_MEM);
    memcpy(buf, s, (size_t)needed);
    return needed;
}

/* ── Row builder ─────────────────────────────────────────────────────────── */

THARAVU_API tde_handle_t THARAVU_CALL tde_row_begin(tde_handle_t table_h)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(table_h);
    if (!th) return set_err_null(TDE_ERR_INVAL);

    row_handle_t *rh = (row_handle_t *)calloc(1, sizeof(row_handle_t));
    if (!rh) return set_err_null(TDE_ERR_MEM);

    rh->cells = (cell_t *)calloc(th->table.col_count, sizeof(cell_t));
    if (!rh->cells) { free(rh); return set_err_null(TDE_ERR_MEM); }

    rh->magic = MAGIC_ROW;
    rh->owner = th;
    return (tde_handle_t)rh;
}

THARAVU_API int THARAVU_CALL tde_row_set_int32(tde_handle_t row_h, int col, int32_t val)
{
    g_last_err = TDE_OK;
    row_handle_t *rh = to_row(row_h);
    if (!rh || col < 0 || col >= rh->owner->table.col_count)
        return set_err(TDE_ERR_INVAL);

    rh->cells[col].type    = DE_TYPE_INT32;
    rh->cells[col].val.i32 = val;
    return TDE_OK;
}

THARAVU_API int THARAVU_CALL tde_row_set_float32(tde_handle_t row_h, int col, float val)
{
    g_last_err = TDE_OK;
    row_handle_t *rh = to_row(row_h);
    if (!rh || col < 0 || col >= rh->owner->table.col_count)
        return set_err(TDE_ERR_INVAL);

    rh->cells[col].type    = DE_TYPE_FLOAT;
    rh->cells[col].val.f32 = val;
    return TDE_OK;
}

THARAVU_API int THARAVU_CALL tde_row_set_string(tde_handle_t row_h, int col, const char *val)
{
    g_last_err = TDE_OK;
    row_handle_t *rh = to_row(row_h);
    if (!rh || col < 0 || col >= rh->owner->table.col_count)
        return set_err(TDE_ERR_INVAL);

    /* Free any previously set string for this column */
    if (rh->cells[col].type == DE_TYPE_STRING && rh->cells[col].val.str)
        free(rh->cells[col].val.str);

    rh->cells[col].type = DE_TYPE_STRING;
    if (val) {
        rh->cells[col].val.str = strdup(val);
        if (!rh->cells[col].val.str) return set_err(TDE_ERR_MEM);
    } else {
        rh->cells[col].val.str = NULL;
    }
    return TDE_OK;
}

THARAVU_API int THARAVU_CALL tde_row_commit(tde_handle_t row_h)
{
    g_last_err = TDE_OK;
    row_handle_t *rh = to_row(row_h);
    if (!rh) return set_err(TDE_ERR_INVAL);

    int res = de_insert_row(&rh->owner->table, rh->cells);

    /* Free the pending row regardless of success/failure */
    for (int c = 0; c < rh->owner->table.col_count; c++)
        if (rh->cells[c].type == DE_TYPE_STRING && rh->cells[c].val.str)
            free(rh->cells[c].val.str);
    free(rh->cells);
    rh->magic = 0;
    free(rh);

    return set_err(res);
}

THARAVU_API void THARAVU_CALL tde_row_discard(tde_handle_t row_h)
{
    row_handle_t *rh = to_row(row_h);
    if (!rh) return;

    for (int c = 0; c < rh->owner->table.col_count; c++)
        if (rh->cells[c].type == DE_TYPE_STRING && rh->cells[c].val.str)
            free(rh->cells[c].val.str);
    free(rh->cells);
    rh->magic = 0;
    free(rh);
}

/* ── Cell update / row delete ────────────────────────────────────────────── */

THARAVU_API int THARAVU_CALL tde_update_int32(tde_handle_t h, int row, int col, int32_t val)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th) return set_err(TDE_ERR_INVAL);

    cell_t c = { DE_TYPE_INT32, 0, {0} };
    c.val.i32 = val;
    return set_err(de_update_cell(&th->table, row, col, &c));
}

THARAVU_API int THARAVU_CALL tde_update_float32(tde_handle_t h, int row, int col, float val)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th) return set_err(TDE_ERR_INVAL);

    cell_t c = { DE_TYPE_FLOAT, 0, {0} };
    c.val.f32 = val;
    return set_err(de_update_cell(&th->table, row, col, &c));
}

THARAVU_API int THARAVU_CALL tde_update_string(tde_handle_t h, int row, int col, const char *val)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th || !val) return set_err(TDE_ERR_INVAL);

    cell_t c = { DE_TYPE_STRING, 0, {0} };
    c.val.str = (char *)val; /* de_update_cell strdups internally */
    return set_err(de_update_cell(&th->table, row, col, &c));
}

THARAVU_API int THARAVU_CALL tde_delete_row(tde_handle_t h, int row)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th) return set_err(TDE_ERR_INVAL);
    return set_err(de_delete_row(&th->table, row));
}

/* ── Query ───────────────────────────────────────────────────────────────── */

THARAVU_API tde_handle_t THARAVU_CALL tde_find(tde_handle_t h,
                                                const char *column,
                                                const char *value)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th || !column || !value) return set_err_null(TDE_ERR_INVAL);

    table_handle_t *result = (table_handle_t *)calloc(1, sizeof(table_handle_t));
    if (!result) return set_err_null(TDE_ERR_MEM);

    int res = de_find_rows(&th->table, column, value, &result->table);
    if (res < 0)
    {
        free(result);
        return set_err_null(res);
    }

    result->magic = MAGIC_TABLE;
    return (tde_handle_t)result;
}

THARAVU_API int THARAVU_CALL tde_find_ids(tde_handle_t h,
                                           const char *column,
                                           const char *value,
                                           int *ids_out,
                                           int max_results)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th || !column || !value || !ids_out || max_results <= 0)
        return set_err(TDE_ERR_INVAL);
    int res = de_find_row_ids(&th->table, column, value, ids_out, max_results);
    if (res < 0) return set_err(res);
    return res; /* total matches, non-negative */
}

/* ── Vocabulary ──────────────────────────────────────────────────────────── */

THARAVU_API int THARAVU_CALL tde_vocab_lookup(tde_handle_t h,
                                               const char *word,
                                               uint32_t   *out_id)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th || !word || !out_id) return set_err(TDE_ERR_INVAL);
    return set_err(de_vocab_lookup_id(&th->table, word, out_id));
}

THARAVU_API int THARAVU_CALL tde_vocab_lookup_batch(tde_handle_t h,
                                                     const char **words,
                                                     int count,
                                                     uint32_t *tokens_out)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th || !words || !tokens_out || count <= 0) return set_err(TDE_ERR_INVAL);
    int res = de_vocab_lookup_batch(&th->table, words, count, tokens_out);
    if (res < 0) return set_err(res);
    return res; /* number resolved — non-negative, not an error code */
}

THARAVU_API const char *THARAVU_CALL tde_vocab_reverse_lookup(tde_handle_t h,
                                                               uint32_t token_id,
                                                               uint16_t *out_len)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th) { set_err(TDE_ERR_INVAL); return NULL; }
    const char *p = de_vocab_reverse_lookup(&th->table, token_id, out_len);
    if (!p) set_err(TDE_ERR_NOTFOUND);
    return p;
}

THARAVU_API const char *THARAVU_CALL tde_vocab_reverse_lookup_ex(tde_handle_t  h,
                                                                   uint32_t      token_id,
                                                                   uint16_t     *out_len,
                                                                   uint16_t     *out_flags,
                                                                   const float **out_vec)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th) { set_err(TDE_ERR_INVAL); return NULL; }
    const char *p = de_vocab_reverse_lookup_ex(&th->table, token_id, out_len, out_flags, out_vec);
    if (!p) set_err(TDE_ERR_NOTFOUND);
    return p;
}

THARAVU_API int THARAVU_CALL tde_vocab_reverse_batch(tde_handle_t h,
                                                      const uint32_t *token_ids,
                                                      int count,
                                                      const char **words_out)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th || !token_ids || !words_out || count <= 0) return set_err(TDE_ERR_INVAL);
    int res = de_vocab_reverse_batch(&th->table, token_ids, count, words_out);
    if (res < 0) return set_err(res);
    return res;
}

/* ── Vectors ─────────────────────────────────────────────────────────────── */

THARAVU_API int THARAVU_CALL tde_vector_get_row(tde_handle_t h,
                                                 uint32_t     row_id,
                                                 float       *buf,
                                                 uint32_t     buf_floats,
                                                 uint32_t    *out_dim)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th || !buf) return set_err(TDE_ERR_INVAL);

    uint32_t dim = 0;
    const float *vec = de_vector_get(&th->table, row_id, &dim);
    if (!vec) return set_err(TDE_ERR_NOTFOUND);
    if (buf_floats < dim) return set_err(TDE_ERR_MEM);

    memcpy(buf, vec, dim * sizeof(float));
    if (out_dim) *out_dim = dim;
    return (int)dim;
}

THARAVU_API const float *THARAVU_CALL tde_vector_get_ptr(tde_handle_t h,
                                                          uint32_t row_id,
                                                          uint32_t *out_dim)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th) { set_err(TDE_ERR_INVAL); return NULL; }
    const float *p = de_vector_get(&th->table, row_id, out_dim);
    if (!p) set_err(TDE_ERR_NOTFOUND);
    return p;
}

THARAVU_API int THARAVU_CALL tde_vector_get_batch(tde_handle_t h,
                                                   const uint32_t *row_ids,
                                                   int count,
                                                   float *out_buf,
                                                   uint32_t *out_dim)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th || !row_ids || !out_buf || count <= 0) return set_err(TDE_ERR_INVAL);
    int res = de_vector_get_batch(&th->table, row_ids, count, out_buf, out_dim);
    if (res < 0) return set_err(res);
    return res;
}

/* ── Top-k vector search ─────────────────────────────────────────────────── */

THARAVU_API int THARAVU_CALL tde_vector_search_topk(tde_handle_t  h,
                                                     const float  *query_vec,
                                                     uint32_t      dim,
                                                     uint32_t      k,
                                                     uint32_t     *ids_out,
                                                     float        *scores_out)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th || !query_vec || dim == 0 || k == 0 || !ids_out || !scores_out)
        return set_err(TDE_ERR_INVAL);
    int res = de_vector_search_topk(&th->table, query_vec, dim, k, ids_out, scores_out);
    if (res < 0) return set_err(res);
    return res; /* result count — non-negative, not an error code */
}

/* ── File builders ───────────────────────────────────────────────────────── */

THARAVU_API int THARAVU_CALL tde_build_vocab(const char     *filepath,
                                              const char    **words,
                                              int             count,
                                              const float    *vectors,
                                              uint32_t        dim,
                                              const uint16_t *flags)
{
    g_last_err = TDE_OK;
    if (!filepath || !words || count <= 0) return set_err(TDE_ERR_INVAL);
    if (dim > 0 && !vectors) return set_err(TDE_ERR_INVAL);
    return set_err(de_build_vocab(words, count, filepath, vectors, dim, flags));
}

THARAVU_API int THARAVU_CALL tde_build_vocab_logical(const char     *logical_name,
                                                      const char    **words,
                                                      int             count,
                                                      const float    *vectors,
                                                      uint32_t        dim,
                                                      const uint16_t *flags)
{
    g_last_err = TDE_OK;
    if (!logical_name || !words || count <= 0) return set_err(TDE_ERR_INVAL);
    if (dim > 0 && !vectors) return set_err(TDE_ERR_INVAL);
    return set_err(de_build_vocab_logical(logical_name, words, count, vectors, dim, flags));
}

/*
 * tde_build_vectors / tde_build_vectors_logical accept a FLAT row-major float
 * array (count * dim floats) and call de_build_vectors_flat() directly —
 * no intermediate float** pointer array is allocated.
 */
THARAVU_API int THARAVU_CALL tde_build_vectors(const char  *filepath,
                                                const float *data,
                                                int          count,
                                                uint32_t     dim)
{
    g_last_err = TDE_OK;
    if (!filepath || !data || count <= 0 || dim == 0) return set_err(TDE_ERR_INVAL);
    return set_err(de_build_vectors_flat(data, count, dim, filepath));
}

THARAVU_API int THARAVU_CALL tde_build_vectors_logical(const char  *logical_name,
                                                        const float *data,
                                                        int          count,
                                                        uint32_t     dim)
{
    g_last_err = TDE_OK;
    if (!logical_name || !data || count <= 0 || dim == 0) return set_err(TDE_ERR_INVAL);
    return set_err(de_build_vectors_flat_logical(logical_name, data, count, dim));
}

/* ── Data Export ─────────────────────────────────────────────────────────── */

THARAVU_API int THARAVU_CALL tde_export_csv(tde_handle_t h, const char *csv_filepath)
{
    g_last_err = TDE_OK;
    table_handle_t *th = to_table(h);
    if (!th || !csv_filepath) return set_err(TDE_ERR_INVAL);

    FILE *f = fopen(csv_filepath, "w");
    if (!f) return set_err(TDE_ERR_IO);

    /* Write CSV header */
    for (int c = 0; c < th->table.col_count; c++) {
        if (c > 0) fprintf(f, ",");
        fprintf(f, "\"%s\"", th->table.columns[c]);
    }
    fprintf(f, "\n");

    /* Write data rows */
    for (int r = 0; r < th->table.row_count; r++) {
        for (int c = 0; c < th->table.col_count; c++) {
            if (c > 0) fprintf(f, ",");

            cell_t cell;
            if (de_get_cell(&th->table, r, c, &cell) != DE_OK) {
                fclose(f);
                return set_err(TDE_ERR_IO);
            }

            switch (cell.type) {
                case DE_TYPE_NULL:
                    fprintf(f, "\"NULL\"");
                    break;
                case DE_TYPE_INT32:
                    fprintf(f, "%d", cell.val.i32);
                    break;
                case DE_TYPE_INT64:
                    fprintf(f, "%lld", (long long)cell.val.i64);
                    break;
                case DE_TYPE_FLOAT:
                    fprintf(f, "%.6f", cell.val.f32);
                    break;
                case DE_TYPE_STRING:
                    if (cell.val.str) {
                        /* Escape quotes and wrap in quotes */
                        fprintf(f, "\"");
                        for (const char *p = cell.val.str; *p; p++) {
                            if (*p == '"') fprintf(f, "\"\"");
                            else fprintf(f, "%c", *p);
                        }
                        fprintf(f, "\"");
                    } else {
                        fprintf(f, "\"NULL\"");
                    }
                    break;
                default:
                    fprintf(f, "\"UNKNOWN\"");
                    break;
            }
        }
        fprintf(f, "\n");
    }

    fclose(f);
    return TDE_OK;
}

/* ── Error handling ──────────────────────────────────────────────────────── */

THARAVU_API const char *THARAVU_CALL tde_strerror(int code)
{
    return de_strerror(code);
}

THARAVU_API int THARAVU_CALL tde_last_error(void)
{
    return g_last_err;
}
