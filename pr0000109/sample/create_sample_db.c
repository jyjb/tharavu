#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "../include/data_engine.h"

#ifdef _WIN32
#include <direct.h>
#define mkdir(path) _mkdir(path)
#endif

static int ensure_dir_recursive(const char *path)
{
    if (!path || !*path)
        return -1;

    char tmp[4096];
    size_t len = strlen(path);
    if (len >= sizeof(tmp))
        return -1;

    strcpy(tmp, path);
    char *p = tmp;
    if (len >= 2 && tmp[1] == ':')
        p += 2;

    for (; *p; p++)
    {
        if (*p == '/' || *p == '\\')
        {
            char save = *p;
            *p = '\0';
            if (tmp[0] != '\0' && strcmp(tmp, ".") != 0 && strcmp(tmp, "..") != 0)
            {
                if (mkdir(tmp) != 0 && errno != EEXIST)
                    return -1;
            }
            *p = save;
        }
    }
    if (tmp[0] != '\0' && strcmp(tmp, ".") != 0 && strcmp(tmp, "..") != 0)
    {
        if (mkdir(tmp) != 0 && errno != EEXIST)
            return -1;
    }
    return 0;
}

int main(void)
{
    de_set_base_path("sample/data");

    const char *sample_dir = "sample/data/demo";
    if (ensure_dir_recursive(sample_dir) != 0)
    {
        perror("Failed to create sample directory");
        return 1;
    }

    const char *cols[] = {"id", "name", "score"};
    table_t *table = de_create_table(cols, 3);
    if (!table)
    {
        fprintf(stderr, "Failed to create sample table\n");
        return 1;
    }

    cell_t row[3];
    row[0].type = DE_TYPE_INT32;
    row[0].val.i32 = 1;
    row[1].type = DE_TYPE_STRING;
    row[1].val.str  = "Alice";
    row[2].type = DE_TYPE_FLOAT;
    row[2].val.f32 = 98.5f;
    if (de_insert_row(table, row) != DE_OK)
    {
        fprintf(stderr, "Failed to insert sample row 1\n");
        de_destroy(table);
        return 1;
    }

    row[0].type = DE_TYPE_INT32;
    row[0].val.i32 = 2;
    row[1].type = DE_TYPE_STRING;
    row[1].val.str  = "Bob";
    row[2].type = DE_TYPE_FLOAT;
    row[2].val.f32 = 84.0f;
    if (de_insert_row(table, row) != DE_OK)
    {
        fprintf(stderr, "Failed to insert sample row 2\n");
        de_destroy(table);
        return 1;
    }

    if (de_save_logical("demo.users", table) != DE_OK)
    {
        fprintf(stderr, "Failed to save users.odat\n");
        de_destroy(table);
        return 1;
    }
    de_destroy(table);

    const char *words[] = {"hello", "world", "ai", "data"};
    if (de_build_vocab_logical("demo.vocab", words, 4) != DE_OK)
    {
        fprintf(stderr, "Failed to build vocab.ovoc\n");
        return 1;
    }

    float vectors[] = {
        0.1f, 0.2f, 0.3f, 0.4f,
        0.5f, 0.6f, 0.7f, 0.8f,
        0.9f, 1.0f, 1.1f, 1.2f,
    };
    if (de_build_vectors_flat_logical("demo.embeddings", vectors, 3, 4) != DE_OK)
    {
        fprintf(stderr, "Failed to build embeddings.ovec\n");
        return 1;
    }

    printf("Sample database created in %s\n", sample_dir);
    return 0;
}
