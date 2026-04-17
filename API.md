# Tharavu Data Engine — API Reference

All functions are exported from `tharavu_dll.h` with C linkage.
Calling convention: `__cdecl` on Windows, platform default on POSIX.
Include **only** `tharavu_dll.h` in consumer code.

---

## Types

### `tde_handle_t`

```c
typedef void *tde_handle_t;
```

Opaque handle returned by all open/create calls.  Never dereference.
`NULL` indicates failure — check `tde_last_error()`.

---

## Constants

### Cell Types

| Constant | Value | Meaning |
|---|---|---|
| `TDE_TYPE_NULL` | `0` | Unset cell |
| `TDE_TYPE_INT32` | `1` | 32-bit signed integer |
| `TDE_TYPE_INT64` | `2` | 64-bit signed integer |
| `TDE_TYPE_FLOAT` | `3` | 32-bit float |
| `TDE_TYPE_STRING` | `4` | NUL-terminated string |
| `TDE_TYPE_BLOB` | `5` | Raw byte blob |

### File Types

| Constant | Value | Meaning |
|---|---|---|
| `TDE_FILE_ODAT` | `1` | Typed row/column table |
| `TDE_FILE_OVOC` | `2` | Vocabulary hash table |
| `TDE_FILE_OVEC` | `3` | Dense float vectors |

### Error Codes

| Constant | Value | Meaning |
|---|---|---|
| `TDE_OK` | `0` | Success |
| `TDE_ERR_IO` | `-1` | File I/O failure |
| `TDE_ERR_CORRUPT` | `-2` | Bad magic bytes / format mismatch |
| `TDE_ERR_MEM` | `-3` | Memory allocation failed |
| `TDE_ERR_LOCK` | `-4` | File lock contention |
| `TDE_ERR_NOTFOUND` | `-5` | Row, token, or file not found |
| `TDE_ERR_INVAL` | `-6` | NULL or out-of-range argument |

### Vocabulary Sentinel

```c
#define TDE_TOKEN_UNKNOWN  UINT32_MAX
```

Written into output arrays by batch vocab functions for words not present in the vocabulary.

---

## Version

### `tde_version_major` / `tde_version_minor`

```c
int tde_version_major(void);
int tde_version_minor(void);
```

Return the compiled library version components.

---

## Configuration

### `tde_set_base_path`

```c
void tde_set_base_path(const char *path);
```

Override the root directory used for logical-name resolution
(`"db.table"` → `{path}/db/table.ext`).  Default: `"./data"`.
Call before any `tde_open_*` or `tde_build_*` function.

---

### `tde_config_load`

```c
int tde_config_load(const char *ini_path);
```

Load engine settings from a `tharavu.ini` file.
Reads `data_dir`, `dim`, `hash_cap`, and `mmap` keys,
then calls `tde_set_base_path()` with the loaded `data_dir`.
Creates the INI file and data directory if they do not exist.

| Parameter | Description |
|---|---|
| `ini_path` | Path to `tharavu.ini`, e.g. `"tharavu.ini"` |

**Returns** `TDE_OK` or a negative error code.

---

## Table Lifecycle

### `tde_open`

```c
tde_handle_t tde_open(const char *filepath);
```

Open an existing file by absolute or relative path.
The file type (ODAT / OVOC / OVEC) is detected from the magic bytes.

**Returns** a handle, or `NULL` on failure.

---

### `tde_open_odat` / `tde_open_ovoc` / `tde_open_ovec`

```c
tde_handle_t tde_open_odat(const char *logical_name);
tde_handle_t tde_open_ovoc(const char *logical_name);
tde_handle_t tde_open_ovec(const char *logical_name);
```

Open a file by logical name (`"dbname.tablename"`), resolving to:

```
{data_dir}/dbname/tablename.odat   (tde_open_odat)
{data_dir}/dbname/tablename.ovoc   (tde_open_ovoc)
{data_dir}/dbname/tablename.ovec   (tde_open_ovec)
```

**Returns** a handle, or `NULL` on failure.

---

### `tde_create`

```c
tde_handle_t tde_create(const char **col_names, int col_count);
```

Create a new empty ODAT table in memory (not yet on disk).

| Parameter | Description |
|---|---|
| `col_names` | Array of `col_count` NUL-terminated column name strings |
| `col_count` | Number of columns |

**Returns** a handle, or `NULL` on failure.

---

### `tde_save` / `tde_save_logical`

```c
int tde_save        (tde_handle_t h, const char *filepath);
int tde_save_logical(tde_handle_t h, const char *logical_name);
```

Persist an ODAT table to disk atomically (write-then-rename).
The in-memory table remains open after the call.

**Returns** `TDE_OK` or a negative error code.

---

### `tde_close`

```c
void tde_close(tde_handle_t h);
```

Release all resources associated with the handle.
Always call `tde_close` — including on error paths.

---

## Schema / Metadata

### `tde_row_count` / `tde_col_count`

```c
int tde_row_count(tde_handle_t h);
int tde_col_count(tde_handle_t h);
```

Return the number of rows or columns in the table.

---

### `tde_file_type`

```c
int tde_file_type(tde_handle_t h);
```

**Returns** a `TDE_FILE_*` constant identifying the open file format.

---

### `tde_col_name`

```c
const char *tde_col_name(tde_handle_t h, int col);
```

Return the name of column `col` (0-based).
Pointer is valid until `tde_close(h)`.

**Returns** `NULL` if `col` is out of range.

---

## Cell Read Accessors

### `tde_get_cell_type`

```c
int tde_get_cell_type(tde_handle_t h, int row, int col);
```

**Returns** a `TDE_TYPE_*` constant, or `TDE_ERR_INVAL` for an invalid `row`/`col`.

---

### `tde_get_int32` / `tde_get_float32`

```c
int tde_get_int32  (tde_handle_t h, int row, int col, int32_t *out);
int tde_get_float32(tde_handle_t h, int row, int col, float   *out);
```

Copy the typed cell value into `*out`.

**Returns** `TDE_OK` or a negative error code.

---

### `tde_get_string`

```c
int tde_get_string(tde_handle_t h, int row, int col, char *buf, int buf_size);
```

Two-call pattern to avoid fixed buffer sizes:

```c
/* Step 1 — query required size */
int size = tde_get_string(h, row, col, NULL, 0);

/* Step 2 — allocate and retrieve */
char *buf = malloc(size);
tde_get_string(h, row, col, buf, size);
printf("%s\n", buf);
free(buf);
```

**Returns** required buffer size (including NUL) when `buf == NULL`,
bytes written when `buf != NULL`, or a negative error code.

---

## Row Builder (ODAT Insert)

### `tde_row_begin`

```c
tde_handle_t tde_row_begin(tde_handle_t table_h);
```

Begin building a new row.
**Returns** a row handle, or `NULL` on error.

---

### `tde_row_set_int32` / `tde_row_set_float32` / `tde_row_set_string`

```c
int tde_row_set_int32  (tde_handle_t row_h, int col, int32_t    val);
int tde_row_set_float32(tde_handle_t row_h, int col, float      val);
int tde_row_set_string (tde_handle_t row_h, int col, const char *val);
```

Set an individual cell in the pending row.  `col` is 0-based.

**Returns** `TDE_OK` or a negative error code.

---

### `tde_row_commit`

```c
int tde_row_commit(tde_handle_t row_h);
```

Insert the pending row into the table.  Invalidates `row_h`.

**Returns** `TDE_OK` or a negative error code.

---

### `tde_row_discard`

```c
void tde_row_discard(tde_handle_t row_h);
```

Cancel the pending row without inserting.  Invalidates `row_h`.

---

## Cell Update / Row Delete

```c
int  tde_update_int32  (tde_handle_t h, int row, int col, int32_t    val);
int  tde_update_float32(tde_handle_t h, int row, int col, float      val);
int  tde_update_string (tde_handle_t h, int row, int col, const char *val);
int  tde_delete_row    (tde_handle_t h, int row);
```

In-memory modifications.  Call `tde_save` or `tde_save_logical` afterwards
to persist changes.

**Returns** `TDE_OK` or a negative error code.

---

## Query

### `tde_find`

```c
tde_handle_t tde_find(tde_handle_t h, const char *column, const char *value);
```

Return a new table containing every row where `column == value`.
String comparison; integers are matched by value.

The result is a new independent handle — call `tde_close()` when done.

**Returns** a handle with the matching rows, or `NULL` if no rows match
or on error (check `tde_last_error()`).

---

### `tde_find_ids`

```c
int tde_find_ids(tde_handle_t h, const char *column, const char *value,
                 int *ids_out, int max_results);
```

Zero-allocation row-index query.  Writes matching row indices into `ids_out`.

| Parameter | Description |
|---|---|
| `ids_out` | Caller-allocated array to receive row indices |
| `max_results` | Capacity of `ids_out` |

**Returns** the total number of matches found.  If the return value exceeds
`max_results`, the output was truncated.

```c
int idx[16];
int total = tde_find_ids(h, "label", "gamma", idx, 16);
/* if total > 16, allocate a larger buffer and retry */
```

---

## Vocabulary (.ovoc)

### `tde_vocab_lookup`

```c
int tde_vocab_lookup(tde_handle_t h, const char *word, uint32_t *out_id);
```

O(1) single word → token ID lookup.

**Returns** `TDE_OK` and sets `*out_id`, or `TDE_ERR_NOTFOUND`.

---

### `tde_vocab_lookup_batch`

```c
int tde_vocab_lookup_batch(tde_handle_t h, const char **words,
                            int count, uint32_t *tokens_out);
```

Bulk word → token mapping.  `words[i]` → `tokens_out[i]`.
Words not present in the vocabulary produce `TDE_TOKEN_UNKNOWN`.
No allocation inside the call.

**Returns** the number of words resolved, or a negative error code.

---

### `tde_vocab_reverse_lookup`

```c
const char *tde_vocab_reverse_lookup(tde_handle_t h,
                                      uint32_t token_id,
                                      uint16_t *out_len);
```

O(1) token ID → word.  Returns a pointer **directly into the mmap region** —
there is **no NUL terminator**.  Always use `out_len`:

```c
uint16_t len = 0;
const char *w = tde_vocab_reverse_lookup(h, token_id, &len);
if (w)
    printf("%.*s\n", (int)len, w);   /* correct */
/* Never: printf("%s\n", w)  — undefined behaviour */
```

Pointer is valid until `tde_close(h)`.

**Returns** `NULL` if `token_id` is out of range.

---

### `tde_vocab_reverse_batch`

```c
int tde_vocab_reverse_batch(tde_handle_t h, const uint32_t *token_ids,
                             int count, const char **words_out);
```

Bulk token → word (zero-copy).  `words_out[i]` is a mmap pointer or `NULL`.
The same NUL-terminator warning applies — use `tde_vocab_reverse_lookup`
to obtain the length for each pointer.

**Returns** number resolved, or a negative error code.

---

## Vectors (.ovec)

### `tde_vector_get_row`

```c
int tde_vector_get_row(tde_handle_t h, uint32_t row_id,
                        float *buf, uint32_t buf_floats, uint32_t *out_dim);
```

Copy the float vector for `row_id` into the caller-allocated buffer `buf`.

| Parameter | Description |
|---|---|
| `buf` | Caller-allocated float array |
| `buf_floats` | Capacity of `buf` in number of floats |
| `out_dim` | If non-NULL, receives the actual vector dimension |

**Returns** number of floats written, or `TDE_ERR_MEM` if `buf_floats < dim`.

---

### `tde_vector_get_ptr`

```c
const float *tde_vector_get_ptr(tde_handle_t h, uint32_t row_id,
                                  uint32_t *out_dim);
```

Zero-copy pointer directly into the mmap region.
Valid until `tde_close(h)`.

**Returns** `NULL` if `row_id` is out of range.

---

### `tde_vector_get_batch`

```c
int tde_vector_get_batch(tde_handle_t h, const uint32_t *row_ids,
                          int count, float *out_buf, uint32_t *out_dim);
```

Bulk vector fetch.  Writes vectors into `out_buf` in row-major order:
`out_buf[i * dim .. i * dim + dim - 1]` = vector for `row_ids[i]`.
`out_buf` must hold `count * dim` floats.
Missing row IDs fill the corresponding slice with `0.0f`.

**Returns** `count` on success, or a negative error code.

---

### `tde_vector_search_topk`

```c
int tde_vector_search_topk(tde_handle_t h, const float *query,
                             uint32_t dim, uint32_t k,
                             uint32_t *ids_out, float *scores_out);
```

Brute-force cosine top-k search over the entire `.ovec` store.

| Parameter | Description |
|---|---|
| `query` | Query vector (`dim` floats) |
| `dim` | Query vector dimension — must match stored vectors |
| `k` | Maximum number of results to return |
| `ids_out` | Caller-allocated `uint32_t[k]` — receives row indices |
| `scores_out` | Caller-allocated `float[k]` — receives cosine scores |

Results are ordered from highest to lowest cosine similarity.

**Returns** the number of results placed in `ids_out` / `scores_out`
(≤ `k`), or a negative error code.

```c
uint32_t ids[5];
float    scores[5];
int found = tde_vector_search_topk(h, query_vec, 64, 5, ids, scores);
for (int i = 0; i < found; i++)
    printf("rank %d  row=%u  score=%.4f\n", i+1, ids[i], scores[i]);
```

---

## File Builders

### `tde_build_vocab` / `tde_build_vocab_logical`

```c
int tde_build_vocab        (const char *filepath,     const char **words, int count);
int tde_build_vocab_logical(const char *logical_name, const char **words, int count);
```

Build a vocabulary (`.ovoc`) file from an array of NUL-terminated strings.
Token ID for `words[i]` equals `i`.

**Returns** `TDE_OK` or a negative error code.

---

### `tde_build_vectors` / `tde_build_vectors_logical`

```c
int tde_build_vectors        (const char *filepath,     const float *data, int count, uint32_t dim);
int tde_build_vectors_logical(const char *logical_name, const float *data, int count, uint32_t dim);
```

Build a vector (`.ovec`) file from a flat row-major float array.
`data` must hold `count * dim` floats laid out as:

```
[vec0_f0, vec0_f1, ..., vec0_f(dim-1), vec1_f0, ...]
```

**Returns** `TDE_OK` or a negative error code.

---

## Data Export

### `tde_export_csv`

```c
int tde_export_csv(tde_handle_t h, const char *csv_filepath);
```

Export a table to CSV.  Column names are written as the header row.

**Returns** `TDE_OK` or a negative error code.

---

## Error Handling

### `tde_last_error`

```c
int tde_last_error(void);
```

Last `TDE_ERR_*` code recorded on the **calling thread**.
Thread-local — each thread maintains its own error state.
Reset to `TDE_OK` at the start of every API call.

---

### `tde_strerror`

```c
const char *tde_strerror(int code);
```

Human-readable description of a `TDE_ERR_*` code.
Returns a pointer to a static string — do not free.

```c
tde_handle_t h = tde_open_odat("demo.words");
if (!h) {
    fprintf(stderr, "open failed: %s\n", tde_strerror(tde_last_error()));
    return 1;
}
```

---

## Threading and Multi-Process Safety

- `tde_last_error()` is thread-local — safe to read from any thread.
- All other state (open handles, config) is **not** thread-safe.
  Protect concurrent access to a shared handle with an external mutex.
- Separate handles opened by separate threads do not share state and
  require no synchronisation.

### Multi-process write safety

All three write paths (`tde_save` / `tde_build_vocab` / `tde_build_vectors`) acquire an
exclusive sidecar lock before writing:

- **Windows:** `LockFileEx` (blocking, process-level)
- **POSIX (Linux / macOS):** `fcntl F_SETLK` (advisory, process-level)

The sidecar file (e.g. `users.odat.lock`) is created alongside the target and held for the
duration of the atomic write-then-rename sequence.  Concurrent readers are never blocked —
they always see either the old complete file or the new complete file; never a partial write.

> **Note — POSIX advisory locking:** `fcntl` locks are advisory, not mandatory.  A process
> that does not call `tde_` write functions and writes directly to the file will bypass the
> lock.  All writers must go through the tharavu API for the guarantee to hold.

---

## Memory Ownership Summary

| Source | Buffer owner | How to release |
|---|---|---|
| `tde_find` return value | Caller | `tde_close(result)` |
| `tde_col_name` return value | DLL (handle lifetime) | Do not free |
| `tde_vocab_reverse_lookup` return value | DLL (mmap, handle lifetime) | Do not free |
| `tde_vector_get_ptr` return value | DLL (mmap, handle lifetime) | Do not free |
| `tde_vector_get_row` `buf` | Caller | `free(buf)` |
| `tde_strerror` return value | DLL (static) | Do not free |
