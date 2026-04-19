#include "../include/data_engine.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>

#include <sys/stat.h> // For mkdir

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define access _access
#define de_unlink _unlink
#else
#include <unistd.h>
#define de_unlink unlink
#endif

/* --- Global State --- */
static char g_base_path[MAX_PATH_LEN] = "./data";

static int ensure_dir_recursive(const char *path);

void de_set_base_path(const char *path)
{
    if (path && strlen(path) < MAX_PATH_LEN)
    {
        strncpy(g_base_path, path, MAX_PATH_LEN - 1);
        g_base_path[MAX_PATH_LEN - 1] = '\0';
        ensure_dir_recursive(g_base_path);
    }
}

const char *de_get_base_path(void)
{
    return g_base_path;
}

/* Helper: Create directory if not exists */
static int ensure_dir_exists(const char *path)
{
#ifdef _WIN32
    return mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

/* Helper: Create a directory path recursively (mkdir -p behavior). */
static int ensure_dir_recursive(const char *path)
{
    if (!path || !*path)
        return DE_ERR_INVAL;

    char tmp[MAX_PATH_LEN];
    size_t len = strlen(path);
    if (len >= sizeof(tmp))
        return DE_ERR_INVAL;

    strcpy(tmp, path);

    /* Skip a Windows drive prefix like "C:\" when iterating. */
    char *p = tmp;
    if (len >= 2 && tmp[1] == ':')
        p += 2;

    for (; *p; p++)
    {
        if (*p == '/' || *p == '\\')
        {
            char saved = *p;
            *p = '\0';
            if (tmp[0] != '\0' && strcmp(tmp, ".") != 0 && strcmp(tmp, "..") != 0)
            {
                if (ensure_dir_exists(tmp) != 0 && errno != EEXIST)
                    return DE_ERR_IO;
            }
            *p = saved;
        }
    }

    if (tmp[0] != '\0' && strcmp(tmp, ".") != 0 && strcmp(tmp, "..") != 0)
    {
        if (ensure_dir_exists(tmp) != 0 && errno != EEXIST)
            return DE_ERR_IO;
    }

    return DE_OK;
}

/* Resolve "db.table" -> "./data/db/table.ext" */
int de_resolve_path(const char *logical_name, int file_type, char *out_path, size_t max_len)
{
    const char *dot = strchr(logical_name, '.');
    if (!dot)
        return DE_ERR_INVAL; // Must be "db.table"

    int db_len = dot - logical_name;
    /* Limit db_name to 63 chars so base_path + "/" + db_name fits in MAX_PATH_LEN */
    if (db_len <= 0 || db_len >= 64)
        return DE_ERR_INVAL;

    char db_name[64];
    strncpy(db_name, logical_name, db_len);
    db_name[db_len] = '\0';

    const char *table_name = dot + 1;
    size_t tname_len = strlen(table_name);
    if (tname_len == 0 || tname_len > 255)
        return DE_ERR_INVAL;

    // Determine extension
    const char *ext = "";
    if (file_type == 1)
        ext = ".odat";
    else if (file_type == 2)
        ext = ".ovoc";
    else if (file_type == 3)
        ext = ".ovec";
    else
        return DE_ERR_INVAL;

    // Construct Path: base/db/table.ext
    int written = snprintf(out_path, max_len, "%s/%s/%s%s", g_base_path, db_name, table_name, ext);

    if (written < 0 || written >= (int)max_len)
        return DE_ERR_MEM;

    // Ensure DB directory and parent directories exist.
    char dir_path[MAX_PATH_LEN];
    int dw = snprintf(dir_path, sizeof(dir_path), "%s/%s", g_base_path, db_name);
    if (dw < 0 || dw >= (int)sizeof(dir_path))
        return DE_ERR_MEM;
    if (ensure_dir_recursive(dir_path) != DE_OK)
        return DE_ERR_IO;

    return DE_OK;
}

/* Forward declarations from platform.c */
int de_platform_lock(int fd, int exclusive);
int de_platform_unlock(int fd);
int de_platform_mmap_readonly(const char *path, void **addr, size_t *len, int *fd_out);
void de_platform_unmap(void *addr, size_t len, int fd);
int de_safe_read(void *base, size_t base_size, uint64_t offset, void *dst, size_t count);

const char *de_strerror(int code)
{
    switch (code)
    {
    case DE_OK:
        return "Success";
    case DE_ERR_IO:
        return "I/O Error";
    case DE_ERR_CORRUPT:
        return "File Corrupted";
    case DE_ERR_MEM:
        return "Out of Memory";
    case DE_ERR_LOCK:
        return "Lock Failed (Busy?)";
    case DE_ERR_NOTFOUND:
        return "Not Found";
    case DE_ERR_INVAL:
        return "Invalid Argument";
    default:
        return "Unknown Error";
    }
}

/* --- Config Loader --- */
static int ensure_parent_dir_exists(const char *path)
{
    if (!path || !*path)
        return DE_ERR_INVAL;

    char tmp[MAX_PATH_LEN];
    size_t len = strlen(path);
    if (len >= sizeof(tmp))
        return DE_ERR_INVAL;

    strcpy(tmp, path);
    char *p = tmp + len;
    while (p > tmp && *p != '/' && *p != '\\')
        p--;
    if (p == tmp)
        return DE_OK; /* No parent directory to create */

    *p = '\0';
    return ensure_dir_recursive(tmp);
}

int de_config_load(const char *ini_path, tharavuConfig *cfg)
{
    memset(cfg, 0, sizeof(tharavuConfig));
    strcpy(cfg->data_dir, "./data");
    cfg->dim = 256;
    cfg->hash_cap = 131072;

    FILE *fp = fopen(ini_path, "r");
    if (!fp)
    {
        if (errno == ENOENT)
        {
            if (ensure_parent_dir_exists(ini_path) != DE_OK)
                return DE_ERR_IO;

            FILE *out = fopen(ini_path, "w");
            if (!out)
                return DE_ERR_IO;

            fprintf(out,
                "# Tharavu default configuration\n"
                "[paths]\n"
                "data_dir = ./data\n"
                "\n"
                "[engine]\n"
                "dim = 256\n"
                "hash_cap = 131072\n"
            );
            fclose(out);
            ensure_dir_recursive(cfg->data_dir);
            return DE_OK;
        }
        return DE_ERR_IO;
    }

    char line[512];
    while (fgets(line, sizeof(line), fp))
    {
        if (line[0] == '#' || line[0] == '[' || line[0] == '\n')
            continue;
        char key[64], val[256];
        if (sscanf(line, "%63[^=] = %255[^\n]", key, val) == 2)
        {
            char *kend = key + strlen(key) - 1;
            while (kend > key && isspace((unsigned char)*kend))
                *kend-- = '\0';

            char *v = val;
            while (*v && isspace((unsigned char)*v))
                v++;
            char *end = v + strlen(v) - 1;
            while (end > v && isspace((unsigned char)*end))
                *end-- = '\0';
            end[1] = '\0';

            if (strcmp(key, "data_dir") == 0)
            {
                strncpy(cfg->data_dir, v, MAX_PATH_LEN - 1);
                cfg->data_dir[MAX_PATH_LEN - 1] = '\0';
            }
            else if (strcmp(key, "dim") == 0)
            {
                char *endptr;
                long val = strtol(v, &endptr, 10);
                if (endptr != v && *endptr == '\0' && val > 0 && val <= 65536)
                    cfg->dim = (int)val;
                /* else: keep default; silently ignore invalid/out-of-range value */
            }
            else if (strcmp(key, "hash_cap") == 0)
            {
                char *endptr;
                long val = strtol(v, &endptr, 10);
                if (endptr != v && *endptr == '\0' && val > 0 && val <= 67108864)
                    cfg->hash_cap = (int)val;
            }
        }
    }
    fclose(fp);

    ensure_dir_recursive(cfg->data_dir);
    return DE_OK;
}

/* --- Table Creation --- */
table_t *de_create_table(const char **col_names, int col_count)
{
    if (!col_names || col_count <= 0)
        return NULL;

    table_t *t = calloc(1, sizeof(table_t));
    if (!t)
        return NULL;

    t->col_count = col_count;
    t->columns = calloc(col_count, sizeof(char *)); // calloc so unset entries are NULL-safe on de_free
    t->row_cap = 10;
    t->rows = malloc(t->row_cap * sizeof(cell_t *));

    if (!t->columns || !t->rows)
    {
        free(t->columns);
        free(t->rows);
        free(t);
        return NULL;
    }

    for (int i = 0; i < col_count; i++)
    {
        t->columns[i] = strdup(col_names[i]);
        if (!t->columns[i])
        {
            de_free(t);
            return NULL;
        }
    }

    t->file_type = 1;

    return t;
}

/* --- Load Logic --- */
int de_load(table_t *table, const char *filepath)
{
    if (!table)
        return DE_ERR_INVAL;
    memset(table, 0, sizeof(table_t));
    strncpy(table->filepath, filepath, MAX_PATH_LEN - 1);

    FILE *fp = fopen(filepath, "rb");
    if (!fp)
        return DE_ERR_IO;

    char magic[4];
    if (fread(magic, 1, 4, fp) != 4)
    {
        fclose(fp);
        return DE_ERR_CORRUPT;
    }
    fseek(fp, 0, SEEK_SET);

    int res = DE_OK;

    if (memcmp(magic, MAGIC_ODAT, 4) == 0)
    {
        table->file_type = 1;
        uint8_t hdr_buf[128];
        if (fread(hdr_buf, 1, 128, fp) != 128)
        {
            res = DE_ERR_CORRUPT;
            goto cleanup;
        }

        odat_header_t h;
        h.row_count = read_u32_le(hdr_buf + 8);
        h.col_count = read_u16_le(hdr_buf + 12);
        h.row_stride = read_u32_le(hdr_buf + 20);
        h.schema_offset = read_u64_le(hdr_buf + 32);
        h.data_offset = read_u64_le(hdr_buf + 40);

        /* Validate header fields against sane limits before any allocation.
         * These caps prevent integer-overflow in malloc and OOM attacks from
         * crafted files.  Limits are generous but finite:
         *   columns : max 4096   (avoids col_count * MAX_COL_NAME overflow)
         *   rows    : max 100 M  (uint32_t could be up to 4 G)
         *   stride  : max 64 MiB (a single serialised row)                  */
        if (h.col_count == 0 || h.col_count > 4096)
        { res = DE_ERR_CORRUPT; goto cleanup; }
        if (h.row_count > 100000000u)
        { res = DE_ERR_CORRUPT; goto cleanup; }
        if (h.row_stride == 0 || h.row_stride > 67108864u)
        { res = DE_ERR_CORRUPT; goto cleanup; }
        /* row_count is capped at 100M above; 100M * sizeof(cell_t*) always
         * fits in size_t on any 32-bit or larger platform. */

        table->row_count      = h.row_count;
        table->col_count      = h.col_count;
        table->max_row_stride = h.row_stride; /* avoids double-scan in de_save */

        // Load Schema — calloc so de_free is safe if strdup fails mid-loop
        table->columns = calloc(h.col_count, sizeof(char *));
        if (!table->columns) { res = DE_ERR_MEM; goto cleanup; }
        fseek(fp, h.schema_offset, SEEK_SET);
        for (int i = 0; i < h.col_count; i++)
        {
            char buf[MAX_COL_NAME];
            if (fread(buf, 1, MAX_COL_NAME, fp) != MAX_COL_NAME)
            {
                res = DE_ERR_CORRUPT;
                goto cleanup;
            }
            buf[MAX_COL_NAME - 1] = '\0'; /* ensure NUL-termination */
            table->columns[i] = strdup(buf);
            if (!table->columns[i]) { res = DE_ERR_MEM; goto cleanup; }
        }

        // Load Data into RAM
        table->rows = malloc(h.row_count * sizeof(cell_t *));
        if (!table->rows) { res = DE_ERR_MEM; goto cleanup; }
        table->row_cap = h.row_count; /* track actual allocated capacity */
        fseek(fp, h.data_offset, SEEK_SET);

        for (uint32_t r = 0; r < h.row_count; r++)
        {
            table->rows[r] = NULL; // keep de_free safe for partial loads
            uint8_t *row_buf = malloc(h.row_stride);
            table->rows[r] = calloc(h.col_count, sizeof(cell_t));
            if (!row_buf || !table->rows[r])
            {
                free(row_buf); // safe even if NULL
                res = DE_ERR_MEM;
                goto cleanup;
            }

            if (fread(row_buf, 1, h.row_stride, fp) != h.row_stride)
            {
                free(row_buf);
                res = DE_ERR_CORRUPT;
                goto cleanup;
            }

            size_t off = 0;
            int inner_res = DE_OK;
            for (int c = 0; c < h.col_count && inner_res == DE_OK; c++)
            {
                cell_t *cell = &table->rows[r][c];
                if (off >= h.row_stride) { inner_res = DE_ERR_CORRUPT; break; }
                cell->type = row_buf[off++];

                if (cell->type == DE_TYPE_INT32)
                {
                    if (off + 4 > h.row_stride) { inner_res = DE_ERR_CORRUPT; break; }
                    memcpy(&cell->val.i32, row_buf + off, 4);
                    off += 4;
                }
                else if (cell->type == DE_TYPE_FLOAT)
                {
                    if (off + 4 > h.row_stride) { inner_res = DE_ERR_CORRUPT; break; }
                    memcpy(&cell->val.f32, row_buf + off, 4);
                    off += 4;
                }
                else if (cell->type == DE_TYPE_STRING)
                {
                    if (off + 2 > h.row_stride) { inner_res = DE_ERR_CORRUPT; break; }
                    uint16_t slen = read_u16_le(row_buf + off);
                    off += 2;
                    if (off + slen > h.row_stride) { inner_res = DE_ERR_CORRUPT; break; }
                    cell->val.str = malloc(slen + 1);
                    if (!cell->val.str) { inner_res = DE_ERR_MEM; break; }
                    memcpy(cell->val.str, row_buf + off, slen);
                    cell->val.str[slen] = '\0';
                    off += slen;
                }
                // Add other types as needed
            }
            free(row_buf); // always freed here — no goto inside inner loop
            if (inner_res != DE_OK) { res = inner_res; goto cleanup; }
        }
    }
    else if (memcmp(magic, MAGIC_OVOC, 4) == 0 || memcmp(magic, MAGIC_OVEC, 4) == 0)
    {
        // MMAP Strategy
        fclose(fp);
        fp = NULL;
        size_t len;
        if (de_platform_mmap_readonly(filepath, &table->mmap_ptr, &len, &table->fd) != DE_OK)
        {
            return DE_ERR_IO;
        }
        table->is_mmap = 1;
        table->mmap_size = len;

        if (magic[2] == 'O' && magic[3] == 'C')
            table->file_type = 2; // OVOC
        else
            table->file_type = 3; // OVEC

        // Basic validation — goto cleanup so mmap/fd are released on error
        if (len < 128)
        {
            res = DE_ERR_CORRUPT;
            goto cleanup;
        }
        uint8_t *hdr = (uint8_t *)table->mmap_ptr;
        uint32_t count = read_u32_le(hdr + 8); // vocab_count or row_count depending on type
        table->row_count = count;

        // Setup dummy columns for API consistency
        table->col_count = 1;
        table->columns = malloc(sizeof(char *));
        table->columns[0] = strdup("data");
    }
    else
    {
        res = DE_ERR_CORRUPT;
    }

cleanup:
    if (fp)
        fclose(fp);
    if (res != DE_OK)
    {
        de_free(table);
        return res;
    }
    return DE_OK;
}

/* --- Atomic Save (ODAT Only for Demo) --- */
int de_save(const table_t *table, const char *filepath)
{
    if (!table || table->file_type != 1)
        return DE_ERR_INVAL; // Only ODAT supported for write in this demo

    /* Acquire an exclusive write lock via a sidecar .lock file.
     * Using a sidecar avoids the Windows restriction that a file
     * cannot be renamed while it is open/locked.                  */
    char lock_path[MAX_PATH_LEN];
    snprintf(lock_path, MAX_PATH_LEN, "%s.lock", filepath);
    int lock_fd = de_platform_open_for_lock(lock_path);
    if (lock_fd != -1)
        de_platform_lock(lock_fd, 1 /* exclusive */);

    // 1. Create Temp File
    char temp_path[MAX_PATH_LEN];
    snprintf(temp_path, MAX_PATH_LEN, "%s.tmp", filepath);

    FILE *fp = fopen(temp_path, "wb");
    if (!fp)
        goto fail;

    // 2. Write Header
    uint8_t hdr[128] = {0};
    memcpy(hdr, MAGIC_ODAT, 4);
    write_u16_le(hdr + 4, VERSION_MAJOR);
    write_u16_le(hdr + 6, VERSION_MINOR);
    write_u32_le(hdr + 8, table->row_count);
    write_u16_le(hdr + 12, table->col_count);

    /* Use the pre-computed max stride tracked during insert/load.
     * Fall back to a single scan only for tables that were built before this
     * field existed (max_row_stride == 0 on tables created with older code). */
    /* Always rescan to get the true max stride — de_update_cell may have
     * changed a cell's type (e.g. INT32 → STRING) without updating the
     * cached max_row_stride, which would cause a buffer-overrun below.   */
    uint32_t stride = 0;
    {
        for (int r = 0; r < table->row_count; r++)
        {
            uint32_t row_bytes = 0;
            for (int c = 0; c < table->col_count; c++)
            {
                cell_t *cell = &table->rows[r][c];
                row_bytes += 1;
                if (cell->type == DE_TYPE_INT32 || cell->type == DE_TYPE_FLOAT)
                    row_bytes += 4;
                else if (cell->type == DE_TYPE_INT64)
                    row_bytes += 8;
                else if (cell->type == DE_TYPE_STRING)
                {
                    /* Cap at UINT16_MAX: strings longer than 65535 bytes cannot
                     * be represented in the on-disk format (2-byte length field). */
                    size_t slen = cell->val.str ? strlen(cell->val.str) : 0;
                    if (slen > 65535u) goto fail; /* reject oversized string */
                    row_bytes += 2 + (uint32_t)slen;
                }
                /* Guard against row_bytes overflow */
                if (row_bytes > 67108864u) goto fail; /* > 64 MiB per row is corrupt */
            }
            if (row_bytes > stride) stride = row_bytes;
        }
    }
    if (stride == 0) stride = (uint32_t)table->col_count + 1; /* safety min for empty tables */
    write_u32_le(hdr + 20, stride);

    uint64_t schema_off = 128;
    uint64_t data_off = schema_off + (table->col_count * MAX_COL_NAME);
    write_u64_le(hdr + 32, schema_off);
    write_u64_le(hdr + 40, data_off);
    time_t now = time(NULL);
    write_u64_le(hdr + 48, (uint64_t)now); // created
    write_u64_le(hdr + 56, (uint64_t)now); // modified

    if (fwrite(hdr, 1, 128, fp) != 128)
        goto fail;

    // 3. Write Schema
    for (int i = 0; i < table->col_count; i++)
    {
        char buf[MAX_COL_NAME] = {0};
        strncpy(buf, table->columns[i], MAX_COL_NAME - 1);
        if (fwrite(buf, 1, MAX_COL_NAME, fp) != MAX_COL_NAME)
            goto fail;
    }

    // 4. Write Data
    uint8_t *row_buf = calloc(1, stride);
    if (!row_buf)
        goto fail;

    for (int r = 0; r < table->row_count; r++)
    {
        memset(row_buf, 0, stride);
        size_t off = 0;
        for (int c = 0; c < table->col_count; c++)
        {
            cell_t *cell = &table->rows[r][c];
            row_buf[off++] = (uint8_t)cell->type;
            if (cell->type == DE_TYPE_INT32)
            {
                write_u32_le(row_buf + off, (uint32_t)cell->val.i32);
                off += 4;
            }
            else if (cell->type == DE_TYPE_FLOAT)
            {
                uint32_t fbits;
                memcpy(&fbits, &cell->val.f32, 4);
                write_u32_le(row_buf + off, fbits);
                off += 4;
            }
            else if (cell->type == DE_TYPE_STRING)
            {
                size_t slen = cell->val.str ? strlen(cell->val.str) : 0;
                if (slen > 65535u)
                {
                    /* String too long for the on-disk format; caller must
                     * have validated before insert.  Abort the write. */
                    free(row_buf);
                    goto fail;
                }
                uint16_t len = (uint16_t)slen;
                write_u16_le(row_buf + off, len);
                off += 2;
                if (len > 0) memcpy(row_buf + off, cell->val.str, len);
                off += len;
            }
        }
        if (fwrite(row_buf, 1, stride, fp) != stride)
        {
            free(row_buf);
            goto fail;
        }
    }
    free(row_buf);
    fclose(fp);
    fp = NULL;

// 5. Atomic Rename
#ifdef _WIN32
    if (MoveFileExA(temp_path, filepath, MOVEFILE_REPLACE_EXISTING) == 0)
        goto fail;
#else
    if (rename(temp_path, filepath) != 0)
        goto fail;
#endif

    if (lock_fd != -1) { de_platform_unlock(lock_fd); de_platform_close_fd(lock_fd); }
    return DE_OK;

fail:
    if (fp) { fclose(fp); }
    de_unlink(temp_path);
    if (lock_fd != -1) { de_platform_unlock(lock_fd); de_platform_close_fd(lock_fd); }
    return DE_ERR_IO;
}

/* --- Read Accessors --- */
int de_get_cell(const table_t *table, int row, int col, cell_t *out)
{
    if (!table || row < 0 || row >= table->row_count || col < 0 || col >= table->col_count)
        return DE_ERR_INVAL;

    if (table->is_mmap)
    {
        // MMAP Logic would go here (parsing variable length on fly)
        // For safety, we require loading into RAM for complex queries in this demo
        return DE_ERR_INVAL;
    }

    *out = table->rows[row][col];
    return DE_OK;
}

int de_find_rows(const table_t *table, const char *column, const char *value, table_t *result)
{
    if (!table || !table->rows || !column || !value || !result)
        return DE_ERR_INVAL;

    int col_idx = -1;
    for (int i = 0; i < table->col_count; i++)
    {
        if (strcmp(table->columns[i], column) == 0)
        {
            col_idx = i;
            break;
        }
    }
    if (col_idx == -1)
        return DE_ERR_NOTFOUND;

    // Zero-init result so de_free is safe if we fail partway through
    memset(result, 0, sizeof(table_t));

    // Deep-copy column names so result is independently freeable via de_free()
    result->col_count = table->col_count;
    result->columns = calloc(table->col_count, sizeof(char *));
    if (!result->columns) return DE_ERR_MEM;
    for (int i = 0; i < table->col_count; i++)
    {
        result->columns[i] = strdup(table->columns[i]);
        if (!result->columns[i]) { de_free(result); return DE_ERR_MEM; }
    }

    result->rows = malloc(table->row_count * sizeof(cell_t *));
    if (!result->rows) { de_free(result); return DE_ERR_MEM; }
    result->row_count = 0;
    result->row_cap = table->row_count;

    for (int r = 0; r < table->row_count; r++)
    {
        cell_t *cell = &table->rows[r][col_idx];
        int match = 0;
        if (cell->type == DE_TYPE_STRING && cell->val.str && strcmp(cell->val.str, value) == 0)
            match = 1;
        else if (cell->type == DE_TYPE_INT32 && atoi(value) == cell->val.i32)
            match = 1;

        if (match)
        {
            // Deep-copy the row so result can be freed independently via de_free()
            cell_t *new_row = calloc(table->col_count, sizeof(cell_t));
            if (!new_row) { de_free(result); return DE_ERR_MEM; }
            for (int c = 0; c < table->col_count; c++)
            {
                new_row[c].type = table->rows[r][c].type;
                new_row[c].size = table->rows[r][c].size;
                if (table->rows[r][c].type == DE_TYPE_STRING && table->rows[r][c].val.str)
                {
                    new_row[c].val.str = strdup(table->rows[r][c].val.str);
                    if (!new_row[c].val.str)
                    {
                        free(new_row);
                        de_free(result);
                        return DE_ERR_MEM;
                    }
                }
                else
                {
                    new_row[c].val = table->rows[r][c].val;
                }
            }
            result->rows[result->row_count++] = new_row;
        }
    }
    return result->row_count;
}

/* --- ZERO-ALLOC ROW-ID QUERY --- */
/*
 * Like de_find_rows but writes matching row indices instead of deep-copying rows.
 * Returns total matches found (may exceed max_results — caller detects truncation).
 * No allocation, no deep copy.
 */
int de_find_row_ids(const table_t *table, const char *column, const char *value,
                    int *ids_out, int max_results)
{
    if (!table || !column || !value || !ids_out || max_results <= 0)
        return DE_ERR_INVAL;
    if (table->is_mmap)
        return DE_ERR_INVAL; /* ODAT in-RAM tables only */

    int col_idx = -1;
    for (int i = 0; i < table->col_count; i++)
    {
        if (strcmp(table->columns[i], column) == 0)
        { col_idx = i; break; }
    }
    if (col_idx < 0) return DE_ERR_NOTFOUND;

    /* Pre-convert value once outside the loop */
    long int ival = strtol(value, NULL, 10);

    int found = 0;
    for (int r = 0; r < table->row_count; r++)
    {
        const cell_t *cell = &table->rows[r][col_idx];
        int match = 0;
        if (cell->type == DE_TYPE_STRING && cell->val.str)
            match = (strcmp(cell->val.str, value) == 0);
        else if (cell->type == DE_TYPE_INT32)
            match = (cell->val.i32 == (int32_t)ival);

        if (match)
        {
            if (found < max_results) ids_out[found] = r;
            found++;
        }
    }
    return found;
}

int de_find_by_id(const table_t *table, uint32_t id, cell_t *out)
{
    if (table->file_type != 3 || !table->is_mmap)
        return DE_ERR_INVAL; /* OVEC only */

    uint8_t       *base = (uint8_t *)table->mmap_ptr;
    ovec_header_t *hdr  = (ovec_header_t *)base;

    if (id >= hdr->row_count)
        return DE_ERR_NOTFOUND;

    uint32_t dim      = hdr->dim;
    uint32_t stride   = hdr->stride;
    uint64_t data_off = hdr->data_offset;
    uint32_t float_off = (hdr->version_minor == 0) ? 32u : 0u;

    uint64_t row_off = data_off + (uint64_t)id * stride;
    if (row_off + float_off + (uint64_t)dim * 4 > table->mmap_size)
        return DE_ERR_CORRUPT;

    out->type      = DE_TYPE_BLOB;
    out->size      = dim * 4;
    out->val.blob  = (void *)(base + row_off + float_off);
    return DE_OK;
}

int de_get_row_raw(const char *filepath, uint32_t row_id, void *buffer, size_t buf_size, uint32_t *out_bytes)
{
    void *addr;
    size_t len;
    int fd;
    if (de_platform_mmap_readonly(filepath, &addr, &len, &fd) != DE_OK)
        return DE_ERR_IO;

    char magic[4];
    memcpy(magic, addr, 4);
    int res = DE_ERR_INVAL;

    if (memcmp(magic, MAGIC_OVEC, 4) == 0)
    {
        ovec_header_t *hdr     = (ovec_header_t *)addr;
        uint32_t       dim     = hdr->dim;
        uint32_t       stride  = hdr->stride;
        uint64_t       data_off = hdr->data_offset;
        uint32_t       float_off = (hdr->version_minor == 0) ? 32u : 0u;
        uint32_t       vec_bytes = dim * (uint32_t)sizeof(float);

        if (row_id >= hdr->row_count)
        { res = DE_ERR_NOTFOUND; goto done; }

        /* buf_size must hold at least the float data (callers care about floats) */
        if (buf_size < vec_bytes)
        { res = DE_ERR_MEM; goto done; }

        uint64_t off = data_off + (uint64_t)row_id * stride;
        if (off + float_off + vec_bytes > len)
        { res = DE_ERR_CORRUPT; goto done; }

        memcpy(buffer, (uint8_t *)addr + off + float_off, vec_bytes);
        *out_bytes = vec_bytes;
        res = DE_OK;
    }

done:
    de_platform_unmap(addr, len, fd);
    return res;
}

void de_free(table_t *table)
{
    if (!table)
        return;

    if (table->is_mmap)
    {
        de_platform_unmap(table->mmap_ptr, table->mmap_size, table->fd);
    }
    else
    {
        if (table->rows)
        {
            for (int i = 0; i < table->row_count; i++)
            {
                if (table->rows[i])
                {
                    for (int c = 0; c < table->col_count; c++)
                    {
                        if (table->rows[i][c].type == DE_TYPE_STRING)
                            free(table->rows[i][c].val.str);
                    }
                    free(table->rows[i]);
                }
            }
            free(table->rows);
        }
    }

    if (table->columns)
    {
        for (int i = 0; i < table->col_count; i++)
            free(table->columns[i]);
        free(table->columns);
    }
    memset(table, 0, sizeof(table_t));
}

/* For tables allocated by de_create_table(): frees internal state AND the struct itself. */
void de_destroy(table_t *table)
{
    if (!table)
        return;
    de_free(table);
    free(table);
}

void de_stats(const table_t *table)
{
    printf("Table: %s\n", table->filepath);
    printf("Type: %s\n", table->file_type == 1 ? "ODAT" : (table->file_type == 2 ? "OVOC" : "OVEC"));
    printf("Rows: %d | Cols: %d\n", table->row_count, table->col_count);
    printf("Storage: %s\n", table->is_mmap ? "MMAP" : "RAM");
}

/* --- Helper: FNV-1a 32-bit Hash (Required for OVOC Lookup) --- */
static uint32_t fnv1a_hash(const char *str)
{
    uint32_t hash = 2166136261u;
    while (*str)
    {
        hash ^= (uint8_t)*str++;
        hash *= 16777619u;
    }
    return hash;
}

/* --- HIGH PERFORMANCE: .ovoc Hash Lookup (O(1)) --- */
/*
 * Looks up a word in the vocabulary file's internal hash table.
 * Returns DE_OK and sets *out_token_id if found.
 * Returns DE_ERR_NOTFOUND if the word doesn't exist.
 * ZERO COPY: Reads directly from mmap memory.
 */
int de_vocab_lookup_id(const table_t *vocab, const char *word, uint32_t *out_token_id)
{
    if (!vocab || !vocab->is_mmap || vocab->file_type != 2 || !word || !out_token_id)
    {
        return DE_ERR_INVAL; // Must be a loaded .ovoc file
    }

    ovoc_header_t *hdr = (ovoc_header_t *)vocab->mmap_ptr;
    uint8_t *base = (uint8_t *)vocab->mmap_ptr;

    // 1. Calculate Hash of the input word
    uint32_t hash = fnv1a_hash(word);
    uint32_t cap = hdr->hash_cap;
    if (cap == 0)
        return DE_ERR_CORRUPT;
    uint32_t index = hash % cap;

    // 2. Locate Hash Table in File — validate it fits within the mapped region
    if (hdr->hash_offset + (uint64_t)cap * sizeof(uint64_t) > vocab->mmap_size)
        return DE_ERR_CORRUPT;
    uint64_t *hash_table = (uint64_t *)(base + hdr->hash_offset);

    // 3. Linear Probe (Open Addressing)
    // We probe until we find the word or an empty slot (0xFFFFFFFFFFFFFFFF)
    uint32_t probes = 0;
    while (probes < cap)
    {
        uint64_t slot_offset = hash_table[index];

        // Empty slot means word not found
        if (slot_offset == 0xFFFFFFFFFFFFFFFFULL)
        {
            return DE_ERR_NOTFOUND;
        }

        // Slot points to a data row.
        // Row Layout: [token_id (4)] [key_len (2)] [flags (2)] [word_string...]
        // Validate the fixed 8-byte header of the row fits in the file.
        if (slot_offset + 8 > vocab->mmap_size)
            return DE_ERR_CORRUPT;

        uint8_t *row_ptr = base + slot_offset;
        uint16_t key_len = read_u16_le(row_ptr + 4);

        // Validate that the word bytes also fit in the file.
        if (slot_offset + 8 + key_len > vocab->mmap_size)
            return DE_ERR_CORRUPT;

        char *row_word = (char *)(row_ptr + 8);

        // Quick length check first (fast fail)
        size_t word_len = strlen(word);
        if (key_len == word_len)
        {
            // Full string comparison
            if (memcmp(row_word, word, key_len) == 0)
            {
                // MATCH FOUND! Extract Token ID
                *out_token_id = read_u32_le(row_ptr);
                return DE_OK;
            }
        }

        // Collision: Move to next slot
        index = (index + 1) % cap;
        probes++;
    }

    return DE_ERR_NOTFOUND; /* table full or corrupted */
}

/* --- BULK: word[] → token[] (no allocation) --- */
int de_vocab_lookup_batch(const table_t *vocab,
                           const char **words, int count,
                           uint32_t *tokens_out)
{
    if (!vocab || !words || !tokens_out || count <= 0)
        return DE_ERR_INVAL;
    int found = 0;
    for (int i = 0; i < count; i++)
    {
        if (words[i] && de_vocab_lookup_id(vocab, words[i], &tokens_out[i]) == DE_OK)
            found++;
        else
            tokens_out[i] = UINT32_MAX; /* TDE_TOKEN_UNKNOWN */
    }
    return found;
}

/* --- REVERSE LOOKUP: token_id → word (zero-copy, O(1)) --- */
/*
 * Requires the file to have been built with de_build_vocab() v1.1+, which
 * writes a uint64_t[vocab_count] reverse index at hdr->reverse_offset.
 * Returns a pointer directly into the mmap region — caller must NOT free it.
 * Returns NULL for out-of-range token_id or for pre-v1.1 files.
 */
const char *de_vocab_reverse_lookup(const table_t *vocab, uint32_t token_id,
                                     uint16_t *out_len)
{
    if (!vocab || !vocab->is_mmap || vocab->file_type != 2)
        return NULL;

    uint8_t       *base = (uint8_t *)vocab->mmap_ptr;
    ovoc_header_t *hdr  = (ovoc_header_t *)base;

    if (token_id >= hdr->vocab_count)
        return NULL;

    uint64_t rev_off = hdr->reverse_offset;
    if (rev_off == 0)
        return NULL; /* pre-v1.1 file: no reverse index */

    /* Validate the entire reverse-index array fits in the mapping */
    if (rev_off + (uint64_t)hdr->vocab_count * sizeof(uint64_t) > vocab->mmap_size)
        return NULL;

    uint64_t row_off = read_u64_le(base + rev_off + (uint64_t)token_id * sizeof(uint64_t));

    /* Validate record header (8 bytes: token_id[4] + key_len[2] + flags[2]) */
    if (row_off + 8 > vocab->mmap_size)
        return NULL;

    uint8_t  *row     = base + row_off;
    uint16_t  key_len = read_u16_le(row + 4);

    if (row_off + 8 + key_len > vocab->mmap_size)
        return NULL;

    if (out_len) *out_len = key_len;
    return (const char *)(row + 8); /* zero-copy pointer into mmap */
}

/* --- BULK REVERSE: token_id[] → word[] (zero-copy, no allocation) --- */
int de_vocab_reverse_batch(const table_t *vocab,
                            const uint32_t *token_ids, int count,
                            const char **words_out)
{
    if (!vocab || !token_ids || !words_out || count <= 0)
        return DE_ERR_INVAL;
    int found = 0;
    for (int i = 0; i < count; i++)
    {
        words_out[i] = de_vocab_reverse_lookup(vocab, token_ids[i], NULL);
        if (words_out[i]) found++;
    }
    return found;
}

/* --- HIGH PERFORMANCE: .ovec Vector Access (O(1)) --- */
/*
 * Retrieves a pointer to the raw float vector for a given row_id.
 * Zero-copy: returns a pointer directly into the mmap region.
 * Do NOT free this pointer; it is valid as long as the table is loaded.
 *
 * Format versions:
 *   v1.0 (version_minor == 0): stride = 32 + dim*4  (32-byte per-row header)
 *   v1.1 (version_minor >= 1): stride = dim*4        (no per-row header)
 * stride is always read from hdr->stride so both versions work transparently.
 */
const float *de_vector_get(const table_t *vectors, uint32_t row_id, uint32_t *out_dim)
{
    if (!vectors || !vectors->is_mmap || vectors->file_type != 3)
        return NULL;

    uint8_t       *base = (uint8_t *)vectors->mmap_ptr;
    ovec_header_t *hdr  = (ovec_header_t *)base;

    if (row_id >= hdr->row_count)
        return NULL;

    uint32_t dim      = hdr->dim;
    uint32_t stride   = hdr->stride;
    uint64_t data_off = hdr->data_offset;

    /* Per-row float offset: v1.0 has a 32-byte dead header before the floats */
    uint32_t float_off = (hdr->version_minor == 0) ? 32u : 0u;

    if (out_dim) *out_dim = dim;

    uint64_t row_off = data_off + (uint64_t)row_id * stride;

    if (row_off + float_off + (uint64_t)dim * sizeof(float) > vectors->mmap_size)
        return NULL;

    return (const float *)(base + row_off + float_off);
}

/* --- BULK: row_id[] → flat float buffer (no allocation) --- */
/*
 * Fills out_buf with count vectors laid out as:
 *   [vec0_f0 .. vec0_f(dim-1) | vec1_f0 .. vec1_f(dim-1) | ...]
 * Missing row IDs write 0.0f-filled slices (caller detects with a sentinel
 * if needed).  Returns count on success or a negative error code.
 */
int de_vector_get_batch(const table_t *vectors,
                         const uint32_t *row_ids, int count,
                         float *out_buf, uint32_t *out_dim)
{
    if (!vectors || !row_ids || !out_buf || count <= 0)
        return DE_ERR_INVAL;

    uint32_t dim = 0;
    /* Probe row 0 just to get the dimension (avoids re-reading header) */
    if (vectors->is_mmap && vectors->file_type == 3)
    {
        ovec_header_t *hdr = (ovec_header_t *)vectors->mmap_ptr;
        dim = hdr->dim;
    }
    if (dim == 0) return DE_ERR_INVAL;
    if (out_dim) *out_dim = dim;

    size_t bytes_per_vec = (size_t)dim * sizeof(float);

    for (int i = 0; i < count; i++)
    {
        const float *v = de_vector_get(vectors, row_ids[i], NULL);
        if (v)
            memcpy(out_buf + (size_t)i * dim, v, bytes_per_vec);
        else
            memset(out_buf + (size_t)i * dim, 0, bytes_per_vec);
    }
    return count;
}

/* --- CRUD: INSERT --- */
/* Adds a new row to the table (in memory). Must call de_save() to persist. */
int de_insert_row(table_t *table, cell_t *row_values)
{
    if (!table || !row_values)
        return DE_ERR_INVAL;
    if (table->is_mmap)
        return DE_ERR_INVAL; // Cannot insert directly into mmap

    // Resize rows array if needed
    if (table->row_count >= table->row_cap)
    {
        int new_cap = (table->row_cap > 0) ? table->row_cap * 2 : 10;
        cell_t **new_rows = realloc(table->rows, new_cap * sizeof(cell_t *));
        if (!new_rows)
            return DE_ERR_MEM;
        table->rows = new_rows;
        table->row_cap = new_cap;
    }

    // Allocate new row
    cell_t *new_row = calloc(table->col_count, sizeof(cell_t));
    if (!new_row)
        return DE_ERR_MEM;

    // Copy values (Deep copy for strings)
    for (int c = 0; c < table->col_count; c++)
    {
        new_row[c].type = row_values[c].type;
        if (row_values[c].type == DE_TYPE_STRING)
        {
            if (row_values[c].val.str)
            {
                /* Reject strings that exceed the on-disk uint16_t length field */
                if (strlen(row_values[c].val.str) > 65535u)
                {
                    free(new_row);
                    return DE_ERR_INVAL;
                }
                new_row[c].val.str = strdup(row_values[c].val.str);
                if (!new_row[c].val.str)
                {
                    free(new_row);
                    return DE_ERR_MEM;
                }
            }
        }
        else
        {
            new_row[c].val = row_values[c].val; // Copy union value
        }
    }

    /* Update the running max stride so de_save() avoids a double-scan */
    {
        uint32_t this_stride = 0;
        for (int c = 0; c < table->col_count; c++)
        {
            this_stride += 1; /* type byte */
            if (new_row[c].type == DE_TYPE_INT32)
                this_stride += 4;
            else if (new_row[c].type == DE_TYPE_FLOAT)
                this_stride += 4;
            else if (new_row[c].type == DE_TYPE_INT64)
                this_stride += 8;
            else if (new_row[c].type == DE_TYPE_STRING)
            {
                /* strlen bounded to 65535 by the check above; cast is safe */
                this_stride += 2 + (uint32_t)(new_row[c].val.str ? strlen(new_row[c].val.str) : 0);
            }
            /* Guard against per-row overflow */
            if (this_stride > 67108864u)
            {
                free(new_row);
                return DE_ERR_INVAL;
            }
        }
        if (this_stride > table->max_row_stride)
            table->max_row_stride = this_stride;
    }

    table->rows[table->row_count] = new_row;
    table->row_count++;
    return DE_OK;
}

/* --- CRUD: UPDATE --- */
/* Updates a specific cell in memory. Must call de_save() to persist. */
int de_update_cell(table_t *table, int row, int col, const cell_t *new_value)
{
    if (!table || row < 0 || row >= table->row_count || col < 0 || col >= table->col_count)
        return DE_ERR_INVAL;
    if (table->is_mmap)
        return DE_ERR_INVAL;

    cell_t *target = &table->rows[row][col];

    // Free old string if exists
    if (target->type == DE_TYPE_STRING && target->val.str)
    {
        free(target->val.str);
        target->val.str = NULL;
    }

    // Set new value
    target->type = new_value->type;
    if (new_value->type == DE_TYPE_STRING)
    {
        if (new_value->val.str)
        {
            target->val.str = strdup(new_value->val.str);
            if (!target->val.str)
                return DE_ERR_MEM;
        }
    }
    else
    {
        target->val = new_value->val;
    }
    return DE_OK;
}

/* --- CRUD: DELETE --- */
/* Removes a row by index. Shifts remaining rows up. Must call de_save() to persist. */
int de_delete_row(table_t *table, int row_index)
{
    if (!table || row_index < 0 || row_index >= table->row_count)
        return DE_ERR_INVAL;
    if (table->is_mmap)
        return DE_ERR_INVAL;

    // Free the row being deleted
    for (int c = 0; c < table->col_count; c++)
    {
        if (table->rows[row_index][c].type == DE_TYPE_STRING && table->rows[row_index][c].val.str)
        {
            free(table->rows[row_index][c].val.str);
        }
    }
    free(table->rows[row_index]);

    // Shift remaining rows up
    for (int i = row_index; i < table->row_count - 1; i++)
    {
        table->rows[i] = table->rows[i + 1];
    }

    table->row_count--;
    // Optional: Shrink capacity if too empty (omitted for simplicity)
    return DE_OK;
}

/* --- HIGH PERFORMANCE: brute-force cosine top-k vector search (.ovec) --- */

typedef struct { float score; uint32_t id; } topk_entry_t;

static void topk_sift_down(topk_entry_t *h, uint32_t n, uint32_t i)
{
    for (;;) {
        uint32_t s = i, l = 2*i+1, r = 2*i+2;
        if (l < n && h[l].score < h[s].score) s = l;
        if (r < n && h[r].score < h[s].score) s = r;
        if (s == i) break;
        topk_entry_t t = h[i]; h[i] = h[s]; h[s] = t;
        i = s;
    }
}

static int topk_cmp_desc(const void *a, const void *b)
{
    float sa = ((const topk_entry_t *)a)->score;
    float sb = ((const topk_entry_t *)b)->score;
    return (sa > sb) ? -1 : (sa < sb) ? 1 : 0;
}

/*
 * Brute-force cosine-similarity top-k search over an entire .ovec store.
 * query_vec must have exactly dim floats matching the file's stored dimension.
 * Results written to ids_out[0..return_value-1] and scores_out[0..return_value-1],
 * sorted best-first (highest cosine similarity first).
 * Returns the number of results written (≤k) or a negative DE_ERR_* code.
 */
int de_vector_search_topk(const table_t *vectors,
                           const float   *query_vec, uint32_t dim,
                           uint32_t       k,
                           uint32_t      *ids_out,   float *scores_out)
{
    if (!vectors || !query_vec || dim == 0 || k == 0 || !ids_out || !scores_out)
        return DE_ERR_INVAL;
    if (!vectors->is_mmap || vectors->file_type != 3)
        return DE_ERR_INVAL;

    ovec_header_t *hdr = (ovec_header_t *)vectors->mmap_ptr;
    uint32_t n = hdr->row_count;
    if (n == 0) return 0;
    if (hdr->dim != dim) return DE_ERR_INVAL;

    uint32_t actual_k = (k < n) ? k : n;

    /* Pre-compute query L2 norm (double accumulator for precision) */
    double qnorm_sq = 0.0;
    for (uint32_t i = 0; i < dim; i++)
        qnorm_sq += (double)query_vec[i] * query_vec[i];
    if (qnorm_sq < 1e-20)
        return 0; /* zero-magnitude query — undefined similarity */
    float qnorm = (float)sqrt(qnorm_sq);

    topk_entry_t *heap = (topk_entry_t *)malloc(actual_k * sizeof(topk_entry_t));
    if (!heap) return DE_ERR_MEM;

    uint32_t filled = 0;

    for (uint32_t row = 0; row < n; row++) {
        uint32_t rdim = 0;
        const float *vec = de_vector_get(vectors, row, &rdim);
        if (!vec || rdim != dim) continue;

        double dot = 0.0, vnorm_sq = 0.0;
        for (uint32_t i = 0; i < dim; i++) {
            dot      += (double)query_vec[i] * vec[i];
            vnorm_sq += (double)vec[i] * vec[i];
        }
        float score;
        if (vnorm_sq < 1e-20) {
            score = 0.0f;
        } else {
            score = (float)(dot / (qnorm * sqrt(vnorm_sq)));
            /* clamp to [-1, 1] against floating-point rounding */
            if (score >  1.0f) score =  1.0f;
            if (score < -1.0f) score = -1.0f;
        }

        if (filled < actual_k) {
            heap[filled].score = score;
            heap[filled].id    = row;
            filled++;
            /* Build min-heap once the buffer is full */
            if (filled == actual_k) {
                for (int32_t i = (int32_t)(actual_k / 2) - 1; i >= 0; i--)
                    topk_sift_down(heap, actual_k, (uint32_t)i);
            }
        } else if (score > heap[0].score) {
            /* New score beats the current worst — replace heap root and re-heapify */
            heap[0].score = score;
            heap[0].id    = row;
            topk_sift_down(heap, actual_k, 0);
        }
    }

    /* Sort final results descending by score */
    qsort(heap, filled, sizeof(topk_entry_t), topk_cmp_desc);

    for (uint32_t i = 0; i < filled; i++) {
        ids_out[i]    = heap[i].id;
        scores_out[i] = heap[i].score;
    }

    free(heap);
    return (int)filled;
}

/* --- VOCABULARY BUILDER (.ovoc) --- */
/*
 * File layout (v1.1):
 *   [  0..127 ] ovoc_header_t
 *   [128..128+cap*8-1] hash table  (uint64_t[cap], sentinel = 0xFFFFFFFFFFFFFFFF)
 *   [data_off .. ] word records    ([token_id u32][key_len u16][flags u16][word bytes])
 *   [rev_off  .. ] reverse index   (uint64_t[vocab_count]: rev[token_id] = file_offset_of_record)
 */
int de_build_vocab(const char **words, int count, const char *filepath)
{
    if (!words || count <= 0 || !filepath)
        return DE_ERR_INVAL;

    /* Acquire exclusive write lock via a sidecar .lock file.
     * Using a sidecar avoids the Windows restriction that a file
     * cannot be renamed while it is open/locked.                  */
    char lock_path[MAX_PATH_LEN];
    snprintf(lock_path, MAX_PATH_LEN, "%s.lock", filepath);
    int lock_fd = de_platform_open_for_lock(lock_path);
    if (lock_fd != -1)
        de_platform_lock(lock_fd, 1 /* exclusive */);

    /* Hash table capacity: next power-of-2 >= count*1.5, minimum 16 */
    uint32_t cap = 16;
    while (cap < (uint32_t)((unsigned)count + (unsigned)count / 2u + 1u))
        cap *= 2;

    uint64_t hash_off = HEADER_SIZE;                      /* 128 */
    uint64_t data_off = hash_off + (uint64_t)cap * sizeof(uint64_t);

    ovoc_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, MAGIC_OVOC, 4);
    hdr.version_major = VERSION_MAJOR;
    hdr.version_minor = 1;           /* v1.1: includes reverse index */
    hdr.vocab_count   = (uint32_t)count;
    hdr.hash_cap      = cap;
    hdr.hash_offset   = hash_off;
    hdr.data_offset   = data_off;

    char temp_path[MAX_PATH_LEN];
    snprintf(temp_path, MAX_PATH_LEN, "%s.tmp", filepath);
    FILE *fp = fopen(temp_path, "wb");
    if (!fp) {
        if (lock_fd != -1) { de_platform_unlock(lock_fd); de_platform_close_fd(lock_fd); }
        return DE_ERR_IO;
    }

    /* Initialise cleanup-tracked pointers to NULL *before* any goto fail,
     * so the fail: label can safely call free() on them unconditionally.    */
    uint64_t *hash_table   = NULL;
    uint64_t *word_offsets = NULL;

    /* Placeholder header – rewritten with final fields at the end */
    if (fwrite(&hdr, 1, sizeof(hdr), fp) != sizeof(hdr))
        goto fail;

    /* In-memory hash table (all slots empty — sentinel = 0xFF…FF) */
    hash_table = (uint64_t *)malloc((size_t)cap * sizeof(uint64_t));
    if (!hash_table) goto fail;
    {
        const uint64_t empty_slot = 0xFFFFFFFFFFFFFFFFULL;
        for (uint32_t i = 0; i < cap; i++) hash_table[i] = empty_slot;
    }

    /* Per-token file offset – used to build the reverse index */
    word_offsets = (uint64_t *)malloc((size_t)count * sizeof(uint64_t));
    if (!word_offsets) goto fail;

    /* --- Seek past the hash-table region before writing word records --- */
    if (fseek(fp, (long)data_off, SEEK_SET) != 0) goto fail;

    {
        const uint64_t empty_slot2 = 0xFFFFFFFFFFFFFFFFULL;
        uint64_t current_data_offset = data_off;

        for (int i = 0; i < count; i++)
        {
            const char *word  = words[i];
            if (!word) goto fail; /* NULL entry in words[] is invalid */
            size_t      raw_len = strlen(word);
            if (raw_len > UINT16_MAX) goto fail; /* word too long for 16-bit key_len field */
            uint16_t    wlen  = (uint16_t)raw_len;
            uint32_t    token = (uint32_t)i;
            uint16_t    flags = 0;

            /* Record offset for the reverse index */
            word_offsets[i] = current_data_offset;

            /* Insert into hash table (open addressing, linear probe) */
            uint32_t hash   = fnv1a_hash(word);
            uint32_t index  = hash % cap;
            uint32_t probes = 0;
            while (probes < cap)
            {
                if (hash_table[index] == empty_slot2)
                { hash_table[index] = current_data_offset; break; }
                index = (index + 1) % cap;
                probes++;
            }
            if (probes >= cap) goto fail; /* hash table full — shouldn't happen */

            /* Write record: [token_id(4)][key_len(2)][flags(2)][word(wlen)] */
            if (fwrite(&token, 1, 4,    fp) != 4    ||
                fwrite(&wlen,  1, 2,    fp) != 2    ||
                fwrite(&flags, 1, 2,    fp) != 2    ||
                fwrite(word,   1, wlen, fp) != wlen)
                goto fail;

            current_data_offset += 4u + 2u + 2u + wlen;
        }

        /* --- Reverse index: uint64_t[count] ordered by token_id --- */
        uint64_t rev_off = current_data_offset;
        if (fwrite(word_offsets, sizeof(uint64_t), (size_t)count, fp) != (size_t)count)
            goto fail;
        free(word_offsets);
        word_offsets = NULL;

        /* --- Hash table (written into the reserved region between header and data) --- */
        if (fseek(fp, (long)hash_off, SEEK_SET) != 0) goto fail;
        if (fwrite(hash_table, sizeof(uint64_t), cap, fp) != cap) goto fail;
        free(hash_table);
        hash_table = NULL;

        /* --- Final header with correct reverse_offset --- */
        hdr.reverse_offset = rev_off;
        hdr.file_size      = rev_off + (uint64_t)count * sizeof(uint64_t);
        hdr.created_at     = (uint64_t)time(NULL);
        hdr.modified_at    = hdr.created_at;
        if (fseek(fp, 0, SEEK_SET) != 0) goto fail;
        if (fwrite(&hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) goto fail;
    } /* end inner scope */

    fclose(fp);
    fp = NULL;

#ifdef _WIN32
    if (MoveFileExA(temp_path, filepath, MOVEFILE_REPLACE_EXISTING) == 0)
    { de_unlink(temp_path); if (lock_fd != -1) { de_platform_unlock(lock_fd); de_platform_close_fd(lock_fd); } return DE_ERR_IO; }
#else
    if (rename(temp_path, filepath) != 0)
    { de_unlink(temp_path); if (lock_fd != -1) { de_platform_unlock(lock_fd); de_platform_close_fd(lock_fd); } return DE_ERR_IO; }
#endif
    if (lock_fd != -1) { de_platform_unlock(lock_fd); de_platform_close_fd(lock_fd); }
    return DE_OK;

fail:
    if (fp)          fclose(fp);
    free(hash_table);    /* NULL-safe: initialised to NULL at top of function */
    free(word_offsets);
    de_unlink(temp_path);
    if (lock_fd != -1) { de_platform_unlock(lock_fd); de_platform_close_fd(lock_fd); }
    return DE_ERR_IO;
}

/* --- VECTOR BUILDER HELPERS --- */
/*
 * Internal: open temp file, write header, write floats, atomic rename.
 * Called by both de_build_vectors (float**) and de_build_vectors_flat (float*).
 *
 * New format (v1.1): stride = dim * sizeof(float), no per-row 32-byte dead header.
 * Old files (v1.0) remain readable via version_minor check in de_vector_get().
 */
static int build_vectors_impl(FILE *fp, const char *temp_path, const char *filepath,
                               int count, uint32_t dim,
                               const float **ptrs,    /* non-NULL → float** path */
                               const float  *flat)    /* non-NULL → flat array path */
{
    ovec_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, MAGIC_OVEC, 4);
    hdr.version_major = VERSION_MAJOR;
    hdr.version_minor = 1;               /* v1.1: no per-row header */
    hdr.row_count     = (uint32_t)count;
    hdr.dim           = dim;
    hdr.stride        = dim * (uint32_t)sizeof(float);
    hdr.data_offset   = HEADER_SIZE;     /* 128 */
    hdr.file_size     = HEADER_SIZE + (uint64_t)count * hdr.stride;
    hdr.created_at    = (uint64_t)time(NULL);
    hdr.modified_at   = hdr.created_at;

    if (fwrite(&hdr, 1, sizeof(hdr), fp) != sizeof(hdr))
        goto fail;

    for (int i = 0; i < count; i++)
    {
        const float *row = ptrs ? ptrs[i] : (flat + (size_t)i * dim);
        if (fwrite(row, sizeof(float), dim, fp) != dim)
            goto fail;
    }

    fclose(fp);
    fp = NULL;

#ifdef _WIN32
    if (MoveFileExA(temp_path, filepath, MOVEFILE_REPLACE_EXISTING) == 0)
    { de_unlink(temp_path); return DE_ERR_IO; }
#else
    if (rename(temp_path, filepath) != 0)
    { de_unlink(temp_path); return DE_ERR_IO; }
#endif
    return DE_OK;

fail:
    if (fp) fclose(fp);
    de_unlink(temp_path);
    return DE_ERR_IO;
}

/* --- VECTOR BUILDER (.ovec) — float** interface (backward compatible) --- */
int de_build_vectors(const float **vectors, int count, uint32_t dim, const char *filepath)
{
    if (!vectors || count <= 0 || dim == 0 || !filepath)
        return DE_ERR_INVAL;

    /* Acquire exclusive write lock via a sidecar .lock file. */
    char lock_path[MAX_PATH_LEN];
    snprintf(lock_path, MAX_PATH_LEN, "%s.lock", filepath);
    int lock_fd = de_platform_open_for_lock(lock_path);
    if (lock_fd != -1)
        de_platform_lock(lock_fd, 1 /* exclusive */);

    char temp_path[MAX_PATH_LEN];
    snprintf(temp_path, MAX_PATH_LEN, "%s.tmp", filepath);
    FILE *fp = fopen(temp_path, "wb");
    int res = fp ? build_vectors_impl(fp, temp_path, filepath, count, dim, vectors, NULL)
                 : DE_ERR_IO;

    if (lock_fd != -1) { de_platform_unlock(lock_fd); de_platform_close_fd(lock_fd); }
    return res;
}

/* --- VECTOR BUILDER (.ovec) — flat float* interface (no intermediate allocation) --- */
/*
 * data: row-major flat float array of count * dim floats.
 * Layout: [vec0_f0..vec0_f(dim-1) | vec1_f0..vec1_f(dim-1) | ...]
 * Preferred over de_build_vectors() when the caller already has a contiguous buffer.
 */
int de_build_vectors_flat(const float *data, int count, uint32_t dim, const char *filepath)
{
    if (!data || count <= 0 || dim == 0 || !filepath)
        return DE_ERR_INVAL;

    /* Acquire exclusive write lock via a sidecar .lock file. */
    char lock_path[MAX_PATH_LEN];
    snprintf(lock_path, MAX_PATH_LEN, "%s.lock", filepath);
    int lock_fd = de_platform_open_for_lock(lock_path);
    if (lock_fd != -1)
        de_platform_lock(lock_fd, 1 /* exclusive */);

    char temp_path[MAX_PATH_LEN];
    snprintf(temp_path, MAX_PATH_LEN, "%s.tmp", filepath);
    FILE *fp = fopen(temp_path, "wb");
    int res = fp ? build_vectors_impl(fp, temp_path, filepath, count, dim, NULL, data)
                 : DE_ERR_IO;

    if (lock_fd != -1) { de_platform_unlock(lock_fd); de_platform_close_fd(lock_fd); }
    return res;
}

/* --- WRAPPERS: Logical Name Support --- */

/* Load: de_load_logical("mydb.users", &table) — detects file type from magic bytes */
int de_load_logical(const char *logical_name, table_t *table) {
    if (!logical_name || !table) return DE_ERR_INVAL;
    char path[MAX_PATH_LEN];
    /* Try each extension in order; de_load() detects the actual type from magic bytes */
    for (int ft = 1; ft <= 3; ft++) {
        if (de_resolve_path(logical_name, ft, path, sizeof(path)) != DE_OK) continue;
        if (access(path, 0) != 0) continue; /* file not found */
        return de_load(table, path);
    }
    return DE_ERR_NOTFOUND;
}

/* Specific Loaders */
int de_load_odat(const char *logical_name, table_t *table) {
    char path[MAX_PATH_LEN];
    if (de_resolve_path(logical_name, 1, path, sizeof(path)) != DE_OK) return DE_ERR_INVAL;
    return de_load(table, path);
}

int de_load_ovoc(const char *logical_name, table_t *table) {
    char path[MAX_PATH_LEN];
    if (de_resolve_path(logical_name, 2, path, sizeof(path)) != DE_OK) return DE_ERR_INVAL;
    return de_load(table, path);
}

int de_load_ovec(const char *logical_name, table_t *table) {
    char path[MAX_PATH_LEN];
    if (de_resolve_path(logical_name, 3, path, sizeof(path)) != DE_OK) return DE_ERR_INVAL;
    return de_load(table, path);
}

/* Save Wrapper */
int de_save_logical(const char *logical_name, const table_t *table) {
    char path[MAX_PATH_LEN];
    if (de_resolve_path(logical_name, table->file_type, path, sizeof(path)) != DE_OK) return DE_ERR_INVAL;
    return de_save(table, path);
}

/* Builder Wrappers */
int de_build_vocab_logical(const char *logical_name, const char **words, int count) {
    char path[MAX_PATH_LEN];
    if (de_resolve_path(logical_name, 2, path, sizeof(path)) != DE_OK) return DE_ERR_INVAL;
    return de_build_vocab(words, count, path);
}

int de_build_vectors_logical(const char *logical_name, const float **vectors, int count, uint32_t dim) {
    char path[MAX_PATH_LEN];
    if (de_resolve_path(logical_name, 3, path, sizeof(path)) != DE_OK) return DE_ERR_INVAL;
    return de_build_vectors(vectors, count, dim, path);
}

int de_build_vectors_flat_logical(const char *logical_name, const float *data, int count, uint32_t dim) {
    char path[MAX_PATH_LEN];
    if (de_resolve_path(logical_name, 3, path, sizeof(path)) != DE_OK) return DE_ERR_INVAL;
    return de_build_vectors_flat(data, count, dim, path);
}