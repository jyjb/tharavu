#ifndef THARAVU_DATA_ENGINE_H
#define THARAVU_DATA_ENGINE_H

#include "tharavu_types.h"
#include "formats.h"
#include <stdio.h>

typedef struct
{
    int type;
    size_t size;
    union
    {
        int32_t i32;
        int64_t i64;
        float f32;
        char *str;
        void *blob;
    } val;
} cell_t;

typedef struct
{
    char **columns;
    int col_count;
    cell_t **rows;
    int row_count;
    int row_cap;

    /* Internal State */
    void *mmap_ptr;
    size_t mmap_size;
    int fd; /* File descriptor for locking/mmap */
    int is_mmap;
    int file_type; /* 1=ODAT, 2=OVOC, 3=OVEC */
    char filepath[MAX_PATH_LEN];
    uint32_t max_row_stride; /* Running maximum of serialised row sizes (ODAT) */
} table_t;

typedef struct
{
    char data_dir[MAX_PATH_LEN];
    int dim;
    int hash_cap;
} tharavuConfig;

/* --- Global Configuration --- */
void de_set_base_path(const char *path);
const char *de_get_base_path(void);

/* Lifecycle */
int de_load(table_t *table, const char *filepath);
int de_save(const table_t *table, const char *filepath);
void de_free(table_t *table);    /* Zero internal state; caller frees stack/heap struct */
void de_destroy(table_t *table); /* de_free + free(table) — for tables from de_create_table() */
table_t *de_create_table(const char **col_names, int col_count);

/* Data Access */
int de_get_cell(const table_t *table, int row, int col, cell_t *out);
int de_find_rows(const table_t *table, const char *column, const char *value, table_t *result);
int de_find_by_id(const table_t *table, uint32_t id, cell_t *out_vector);

/* Raw Access (Safe) */
int de_get_row_raw(const char *filepath, uint32_t row_id, void *buffer, size_t buf_size, uint32_t *out_bytes);

/* Utils */
int de_config_load(const char *ini_path, tharavuConfig *cfg);
void de_stats(const table_t *table);
const char *de_strerror(int code);

/* --- CRUD Operations --- */
/* Insert a new row (in memory). Call de_save() to write to disk. */
int de_insert_row(table_t *table, cell_t *row_values);
/* Update a single cell (in memory). Call de_save() to write to disk. */
int de_update_cell(table_t *table, int row, int col, const cell_t *new_value);
/* Delete a row by index (in memory). Call de_save() to write to disk. */
int de_delete_row(table_t *table, int row_index);

int de_build_vocab(const char **words, int count, const char *filepath,
                   const float *vectors, uint32_t dim, const uint16_t *flags);
int de_build_vectors(const float **vectors, int count, uint32_t dim, const char *filepath);
int de_build_vectors_flat(const float *data, int count, uint32_t dim, const char *filepath);
int de_build_vectors_flat_logical(const char *logical_name, const float *data, int count, uint32_t dim);

/* --- Internal Platform Helpers (Defined in src/platform.c) --- */
/* Required for endian-safe binary access and OS abstraction */

uint16_t read_u16_le(const uint8_t *p);
void write_u16_le(uint8_t *p, uint16_t v);
uint32_t read_u32_le(const uint8_t *p);
void write_u32_le(uint8_t *p, uint32_t v);
uint64_t read_u64_le(const uint8_t *p);
void write_u64_le(uint8_t *p, uint64_t v);

int de_platform_lock(int fd, int exclusive);
int de_platform_unlock(int fd);
int de_platform_mmap_readonly(const char *path, void **addr, size_t *len, int *fd_out);
void de_platform_unmap(void *addr, size_t len, int fd);
int de_safe_read(void *base, size_t base_size, uint64_t offset, void *dst, size_t count);
int de_platform_open_for_lock(const char *path);
void de_platform_close_fd(int fd);

/* Converts "dbname.tablename" to full path with extension based on type */
int de_resolve_path(const char *logical_name, int file_type, char *out_path, size_t max_len);

/* --- Configuration --- */
void de_set_base_path(const char *path);

/* --- Logical Name Operations (dbname.tablename) --- */
/* Loaders (Type-specific to avoid ambiguity) */
int de_load_odat(const char *logical_name, table_t *table);
int de_load_ovoc(const char *logical_name, table_t *table);
int de_load_ovec(const char *logical_name, table_t *table);

/* Savers */
int de_save_logical(const char *logical_name, const table_t *table);

/* Builders */
int de_build_vocab_logical(const char *logical_name, const char **words, int count,
                            const float *vectors, uint32_t dim, const uint16_t *flags);
int de_build_vectors_logical(const char *logical_name, const float **vectors, int count, uint32_t dim);

/* --- High Performance Accessors --- */

/* Vocabulary — forward (word → token) */
int de_vocab_lookup_id(const table_t *vocab, const char *word, uint32_t *out_token_id);

/* Bulk forward: resolves count words; missing entries write UINT32_MAX.
 * Returns number of words resolved. No per-call allocation. */
int de_vocab_lookup_batch(const table_t *vocab,
                           const char **words, int count,
                           uint32_t *tokens_out);

/* Vocabulary — reverse (token → word, zero-copy pointer into mmap) */
const char *de_vocab_reverse_lookup(const table_t *vocab, uint32_t token_id,
                                     uint16_t *out_len);
/* Extended reverse lookup: also returns per-token flags and zero-copy vector pointer.
 * out_vec is set to NULL when the file has no embedded vectors (dim == 0). */
const char *de_vocab_reverse_lookup_ex(const table_t *vocab, uint32_t token_id,
                                        uint16_t *out_len, uint16_t *out_flags,
                                        const float **out_vec);

/* Bulk reverse: fills words_out[i] with mmap pointer or NULL.
 * Returns number resolved. No allocation. */
int de_vocab_reverse_batch(const table_t *vocab,
                            const uint32_t *token_ids, int count,
                            const char **words_out);

/* Vectors */
const float *de_vector_get(const table_t *vectors, uint32_t row_id, uint32_t *out_dim);

/* Bulk vector fetch into caller-allocated flat buffer (count * dim floats).
 * Missing rows are filled with 0.0f. Returns count, or negative error. */
int de_vector_get_batch(const table_t *vectors,
                         const uint32_t *row_ids, int count,
                         float *out_buf, uint32_t *out_dim);

/* Build vectors from a flat row-major float array (no intermediate float** needed). */
int de_build_vectors_flat(const float *data, int count, uint32_t dim,
                           const char *filepath);

/* Table row-ID query (zero-alloc: writes row indices, not a deep-copied table).
 * Returns total matches found; may exceed max_results (caller detects truncation). */
int de_find_row_ids(const table_t *table, const char *column, const char *value,
                    int *ids_out, int max_results);

/* Brute-force cosine-similarity top-k search over an entire .ovec store.
 * query_vec: float[dim] query embedding. dim must match the file's stored dimension.
 * k:         maximum number of results.
 * ids_out:   caller-allocated uint32_t[k].
 * scores_out: caller-allocated float[k].
 * Results sorted best-first. Returns result count (≤k) or negative DE_ERR_* code. */
int de_vector_search_topk(const table_t *vectors,
                           const float   *query_vec, uint32_t dim,
                           uint32_t       k,
                           uint32_t      *ids_out,   float *scores_out);

#endif /* THARAVU_DATA_ENGINE_H */
