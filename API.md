# API Reference

## Public functions (shared library API)

All functions are exported from `tharavu_dll.h` and use the C calling
convention. The shared library exports the `tde_` prefixed API for
language bindings and foreign function interfaces.

### Configuration

- `int tde_config_load(const char *ini_path)`
  - Load `tharavu.ini` and set the base data path from its `data_dir`.
  - Returns `TDE_OK` on success or a negative error code on failure.

- `void tde_set_base_path(const char *path)`
  - Override the library base path for logical name resolution.

### Lifecycle

- `tde_handle_t tde_open(const char *filepath)`
  - Open an existing `.odat`, `.ovoc`, or `.ovec` file by path.
  - Returns a handle on success or `NULL` on failure.

- `tde_handle_t tde_open_odat(const char *logical_name)`
  - Open an ODAT file by logical name `dbname.table`.

- `tde_handle_t tde_open_ovoc(const char *logical_name)`
  - Open an OVOC file by logical name.

- `tde_handle_t tde_open_ovec(const char *logical_name)`
  - Open an OVEC file by logical name.

- `tde_handle_t tde_create(const char **col_names, int col_count)`
  - Create a new in-memory ODAT table.
  - Returns a handle or `NULL` on allocation failure.

- `int tde_save(tde_handle_t h, const char *filepath)`
  - Persist an ODAT table to disk atomically.
  - Returns `TDE_OK` on success.

- `int tde_save_logical(tde_handle_t h, const char *logical_name)`
  - Save an ODAT table using a logical name instead of a filesystem path.

- `void tde_close(tde_handle_t h)`
  - Release resources for a table handle.

### Metadata

- `int tde_row_count(tde_handle_t h)`
  - Returns the number of rows in the table.

- `int tde_col_count(tde_handle_t h)`
  - Returns the number of columns.

- `int tde_file_type(tde_handle_t h)`
  - Returns `TDE_FILE_ODAT`, `TDE_FILE_OVOC`, or `TDE_FILE_OVEC`.

- `const char *tde_col_name(tde_handle_t h, int col)`
  - Returns column name string for ODAT tables.

### Cell access

- `int tde_get_cell_type(tde_handle_t h, int row, int col)`
  - Returns the cell type constant `TDE_TYPE_*`.

- `int tde_get_int32(tde_handle_t h, int row, int col, int32_t *out)`
- `int tde_get_float32(tde_handle_t h, int row, int col, float *out)`
- `int tde_get_string(tde_handle_t h, int row, int col, char *buf, int buf_size)`
  - Query string size by passing `buf=NULL`.

### Row builder

- `tde_handle_t tde_row_begin(tde_handle_t table_h)`
- `int tde_row_set_int32(tde_handle_t row_h, int col, int32_t val)`
- `int tde_row_set_float32(tde_handle_t row_h, int col, float val)`
- `int tde_row_set_string(tde_handle_t row_h, int col, const char *val)`
- `int tde_row_commit(tde_handle_t row_h)`
- `void tde_row_discard(tde_handle_t row_h)`

### Query and mutation

- `int tde_update_int32(tde_handle_t h, int row, int col, int32_t val)`
- `int tde_update_float32(tde_handle_t h, int row, int col, float val)`
- `int tde_update_string(tde_handle_t h, int row, int col, const char *val)`
- `int tde_delete_row(tde_handle_t h, int row)`
- `tde_handle_t tde_find(tde_handle_t h, const char *column, const char *value)`

### Vocabulary

- `int tde_vocab_lookup(tde_handle_t h, const char *word, uint32_t *out_id)`
- `int tde_vocab_lookup_batch(tde_handle_t h, const char **words, int count, uint32_t *tokens_out)`
- `const char *tde_vocab_reverse_lookup(tde_handle_t h, uint32_t token_id, uint16_t *out_len)`
- `int tde_vocab_reverse_batch(tde_handle_t h, const uint32_t *token_ids, int count, const char **words_out)`

### Vectors

- `int tde_vector_get_row(tde_handle_t h, uint32_t row_id, float *buf, uint32_t buf_floats, uint32_t *out_dim)`
- `const float *tde_vector_get_ptr(tde_handle_t h, uint32_t row_id, uint32_t *out_dim)`
- `int tde_build_vectors(const float **vectors, int count, uint32_t dim, const char *filepath)`
- `int tde_build_vectors_flat(const float *data, int count, uint32_t dim, const char *filepath)`

### Error handling

- `int tde_last_error(void)`
- `const char *tde_strerror(int code)`

## Example

```c
#include "tharavu_dll.h"
#include <stdio.h>

int main(void) {
    tde_config_load("tharavu.ini");
    const char *cols[] = {"id", "name"};
    tde_handle_t t = tde_create(cols, 2);
    tde_handle_t row = tde_row_begin(t);
    tde_row_set_int32(row, 0, 1);
    tde_row_set_string(row, 1, "Alice");
    tde_row_commit(row);
    tde_save(t, "./data/demo/users.odat");
    tde_close(t);
    return 0;
}
```
