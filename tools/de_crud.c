#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>  
#include "../include/data_engine.h"

void print_usage() {
    printf("Usage:\n");
    printf("  de_crud <file.odat> list\n");
    printf("  de_crud <file.odat> read <row_idx>\n");
    printf("  de_crud <file.odat> insert <col1=val1,col2=val2,...>\n");
    printf("  de_crud <file.odat> update <row_idx> <col_idx> <new_value>\n");
    printf("  de_crud <file.odat> delete <row_idx>\n");
    printf("  de_crud <file.odat> export <csv_file>\n");
}

int main(int argc, char *argv[]) {
    if (argc < 3) { print_usage(); return 1; }

    const char *filepath = argv[1];
    const char *action = argv[2];

    table_t table;
    if (de_load(&table, filepath) != DE_OK) {
        printf("Error: Cannot load file.\n");
        return 1;
    }

    int res = DE_OK;

    if (strcmp(action, "list") == 0) {
        printf("Rows: %d | Cols: %d\n", table.row_count, table.col_count);
        for(int r=0; r<table.row_count; r++) {
            printf("[%d] ", r);
            for(int c=0; c<table.col_count; c++) {
                cell_t val;
                de_get_cell(&table, r, c, &val);
                if(val.type == DE_TYPE_STRING) printf("%s ", val.val.str);
                else if(val.type == DE_TYPE_INT32) printf("%d ", val.val.i32);
                else printf("NULL ");
            }
            printf("\n");
        }
    }
    else if (strcmp(action, "read") == 0) {
        if (argc < 4) { printf("Usage: de_crud <file> read <row_idx>\n"); res = DE_ERR_INVAL; goto cleanup; }

        char *endptr;
        long row_idx_l = strtol(argv[3], &endptr, 10);
        if (endptr == argv[3] || *endptr != '\0') {
            printf("Error: Row index must be a number.\n");
            res = DE_ERR_INVAL;
            goto cleanup;
        }
        int row_idx = (int)row_idx_l;
        if (row_idx < 0 || row_idx >= table.row_count) {
            printf("Error: Row index %d out of bounds.\n", row_idx);
            res = DE_ERR_INVAL;
            goto cleanup;
        }

        printf("Row [%d]:\n", row_idx);
        for (int c = 0; c < table.col_count; c++) {
            cell_t val;
            de_get_cell(&table, row_idx, c, &val);
            printf("  %-15s: ", table.columns[c]);
            if (val.type == DE_TYPE_STRING) printf("%s\n", val.val.str ? val.val.str : "NULL");
            else if (val.type == DE_TYPE_INT32) printf("%d\n", val.val.i32);
            else printf("NULL\n");
        }
    }
    else if (strcmp(action, "insert") == 0) {
        if (argc < 4) { printf("Missing values.\n"); res = DE_ERR_INVAL; goto cleanup; }
        
        cell_t *new_row = calloc(table.col_count, sizeof(cell_t));
        char *vals = strdup(argv[3]);
        char *token = strtok(vals, ",");
        int c_idx = 0;
        
        while(token && c_idx < table.col_count) {
            char *eq = strchr(token, '=');
            if(eq) {
                *eq = '\0';
                char *val_str = eq + 1;
                int is_num = 1;
                char *p = val_str;
                if(*p == '-') p++;
                while(*p) { if(!isdigit((unsigned char)*p)) { is_num=0; break; } p++; }
                
                if(is_num) {
                    new_row[c_idx].type = DE_TYPE_INT32;
                    new_row[c_idx].val.i32 = atoi(val_str);
                } else {
                    new_row[c_idx].type = DE_TYPE_STRING;
                    new_row[c_idx].val.str = strdup(val_str);
                }
            }
            token = strtok(NULL, ",");
            c_idx++;
        }
        free(vals);

        res = de_insert_row(&table, new_row);
        if(res == DE_OK) printf("Row inserted (in memory).\n");
        
        for(int i=0; i<table.col_count; i++) if(new_row[i].type == DE_TYPE_STRING) free(new_row[i].val.str);
        free(new_row);
    }
    else if (strcmp(action, "update") == 0) {
        if (argc < 6) { printf("Missing args.\n"); res = DE_ERR_INVAL; goto cleanup; }
        char *ep1, *ep2;
        long row_l = strtol(argv[3], &ep1, 10);
        long col_l = strtol(argv[4], &ep2, 10);
        if (ep1 == argv[3] || *ep1 != '\0' || ep2 == argv[4] || *ep2 != '\0') {
            printf("Error: row and col must be numbers.\n"); res = DE_ERR_INVAL; goto cleanup;
        }
        int row = (int)row_l;
        int col = (int)col_l;
        char *val = argv[5];
        
        cell_t new_val;
        int is_num = 1; char *p = val; if(*p=='-') p++; while(*p) { if(!isdigit((unsigned char)*p)) { is_num=0; break;} p++; }
        
        if(is_num) { new_val.type = DE_TYPE_INT32; new_val.val.i32 = atoi(val); }
        else { new_val.type = DE_TYPE_STRING; new_val.val.str = val; }
        
        res = de_update_cell(&table, row, col, &new_val);
        if(res == DE_OK) printf("Cell [%d][%d] updated.\n", row, col);
    }
    else if (strcmp(action, "delete") == 0) {
        if (argc < 4) { printf("Missing row index.\n"); res = DE_ERR_INVAL; goto cleanup; }
        char *ep;
        long row_l = strtol(argv[3], &ep, 10);
        if (ep == argv[3] || *ep != '\0') {
            printf("Error: Row index must be a number.\n"); res = DE_ERR_INVAL; goto cleanup;
        }
        int row = (int)row_l;
        res = de_delete_row(&table, row);
        if(res == DE_OK) printf("Row %d deleted.\n", row);
    }
    else if (strcmp(action, "export") == 0) {
        if (argc < 4) { printf("Usage: de_crud <file> export <csv_file>\n"); res = DE_ERR_INVAL; goto cleanup; }
        const char *csv_path = argv[3];
        
        FILE *f = fopen(csv_path, "w");
        if (!f) { printf("Error: Cannot create CSV file.\n"); res = DE_ERR_IO; goto cleanup; }
        
        /* Write CSV header */
        for (int c = 0; c < table.col_count; c++) {
            if (c > 0) fprintf(f, ",");
            fprintf(f, "\"%s\"", table.columns[c]);
        }
        fprintf(f, "\n");
        
        /* Write data rows */
        for (int r = 0; r < table.row_count; r++) {
            for (int c = 0; c < table.col_count; c++) {
                if (c > 0) fprintf(f, ",");
                
                cell_t cell;
                if (de_get_cell(&table, r, c, &cell) != DE_OK) {
                    fclose(f);
                    printf("Error: Failed to read cell.\n");
                    res = DE_ERR_IO;
                    goto cleanup;
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
        printf("Data exported to %s\n", csv_path);
    }
    else {
        printf("Unknown action.\n");
        res = DE_ERR_INVAL;
    }

cleanup:
    if (res == DE_OK && strcmp(action, "list") != 0 && strcmp(action, "read") != 0 && strcmp(action, "export") != 0) {
        printf("Saving changes to disk...\n");
        if (de_save(&table, filepath) == DE_OK) {
            printf("SUCCESS: File updated.\n");
        } else {
            printf("ERROR: Failed to save file.\n");
        }
    }

    de_free(&table);
    return (res == DE_OK) ? 0 : 1;
}