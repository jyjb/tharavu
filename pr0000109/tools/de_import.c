#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../include/data_engine.h"

#define MAX_LINE_LEN 4096
#define MAX_COLS 64

/* Helper: Trim whitespace */
static char *trim(char *str)
{
    char *end;
    while (isspace((unsigned char)*str))
        str++;
    if (*str == 0)
        return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;
    end[1] = '\0';
    return str;
}

/* Helper: Check if string is an integer */
static int is_integer(const char *str)
{
    if (!str || !*str)
        return 0;
    if (*str == '-')
        str++;
    while (*str)
    {
        if (!isdigit((unsigned char)*str))
            return 0;
        str++;
    }
    return 1;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("Usage: de_import <input.csv> <output.odat>\n");
        return 1;
    }

    const char *csv_path = argv[1];
    const char *odat_path = argv[2];

    FILE *fp = fopen(csv_path, "r");
    if (!fp)
    {
        perror("Cannot open CSV");
        return 1;
    }

    printf("Importing %s -> %s ...\n", csv_path, odat_path);

    char line[MAX_LINE_LEN];
    char *cols[MAX_COLS];
    int col_count = 0;

    // 1. Read Header Row
    if (!fgets(line, sizeof(line), fp))
    {
        fprintf(stderr, "Empty CSV file\n");
        fclose(fp);
        return 1;
    }

    // Parse header (simple comma split)
    char *token = strtok(line, ",");
    while (token != NULL && col_count < MAX_COLS)
    {
        cols[col_count] = strdup(trim(token));
        if (!cols[col_count])
        {
            fprintf(stderr, "Out of memory reading column headers\n");
            for (int i = 0; i < col_count; i++) free(cols[i]);
            fclose(fp);
            return 1;
        }
        col_count++;
        token = strtok(NULL, ",");
    }

    if (col_count == 0)
    {
        fprintf(stderr, "No columns found in header\n");
        fclose(fp);
        return 1;
    }

    printf("Found %d columns: ", col_count);
    for (int i = 0; i < col_count; i++)
        printf("%s ", cols[i]);
    printf("\n");

    // 2. Create Table Structure
    table_t *table = de_create_table((const char **)cols, col_count);
    /* col names have been deep-copied by de_create_table; free our copies now */
    for (int i = 0; i < col_count; i++) { free(cols[i]); cols[i] = NULL; }
    if (!table)
    {
        fprintf(stderr, "Failed to create table\n");
        fclose(fp);
        return 1;
    }

    // Pre-allocate rows (we will grow dynamically if needed, but start with 100)
    table->row_cap = 100;
    table->rows = malloc(table->row_cap * sizeof(cell_t *));
    table->row_count = 0;

    // 3. Read Data Rows
    while (fgets(line, sizeof(line), fp))
    {
        // Skip empty lines
        if (strlen(trim(line)) == 0)
            continue;

        if (table->row_count >= table->row_cap)
        {
            int new_cap = table->row_cap * 2;
            cell_t **new_rows = realloc(table->rows, new_cap * sizeof(cell_t *));
            if (!new_rows)
            {
                fprintf(stderr, "Out of memory growing row buffer\n");
                fclose(fp);
                de_free(table);
                free(table);
                return 1;
            }
            table->rows = new_rows;
            table->row_cap = new_cap;
        }

        cell_t *row = calloc(col_count, sizeof(cell_t));
        table->rows[table->row_count] = row;

        // Parse CSV line
        int c_idx = 0;
        token = strtok(line, ",");
        while (token != NULL && c_idx < col_count)
        {
            char *val = trim(token);

            if (is_integer(val))
            {
                row[c_idx].type = DE_TYPE_INT32;
                row[c_idx].val.i32 = atoi(val);
            }
            else
            {
                row[c_idx].type = DE_TYPE_STRING;
                row[c_idx].val.str = strdup(val);
                if (!row[c_idx].val.str)
                {
                    fprintf(stderr, "Out of memory copying cell value\n");
                    fclose(fp);
                    de_free(table);
                    free(table);
                    return 1;
                }
            }
            c_idx++;
            token = strtok(NULL, ",");
        }

        // Fill remaining columns with NULL if CSV row was short
        while (c_idx < col_count)
        {
            row[c_idx].type = DE_TYPE_NULL;
            c_idx++;
        }

        table->row_count++;
    }

    fclose(fp);

    printf("Loaded %d rows.\n", table->row_count);
    printf("Saving to binary format...\n");

    // 4. Save to ODAT
    int res = de_save(table, odat_path);

    if (res == DE_OK)
    {
        printf("SUCCESS! Created %s (%d rows, %d cols)\n", odat_path, table->row_count, col_count);
    }
    else
    {
        printf("FAILED: %s\n", de_strerror(res));
    }

    de_free(table);
    return (res == DE_OK) ? 0 : 1;
}