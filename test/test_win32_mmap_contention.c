/*
 * test_win32_mmap_contention.c
 *
 * Regression test for de_platform_atomic_rename() retry logic (src/platform.c).
 *
 * Scenario:
 *   Thread A (reader)   — holds a read-only mmap of the .ovec destination file.
 *   Thread B (releaser) — waits 80 ms then drops the mmap, giving the rename
 *                         time to fail once before it can succeed.
 *   Main thread         — calls de_build_vectors_flat() which internally does
 *                         write-to-tmp + atomic rename while A holds the mapping.
 *
 * Expected result:
 *   de_build_vectors_flat() returns DE_OK.  The final file contains the new
 *   data.  No leftover .tmp file remains.
 *
 * Compile (MinGW, from repo root):
 *   gcc -std=c99 -O2 -I./include \
 *       tests/test_win32_mmap_contention.c src/data_engine.c src/platform.c \
 *       -o tests/test_mmap_contention.exe -lm
 *
 * Run:
 *   tests\test_mmap_contention.exe
 *
 * Exit code: 0 = PASS, 1 = FAIL, 77 = SKIP (non-Windows).
 */

#ifdef _WIN32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "../src/include/data_engine.h"

#define TEST_FILE  "test\\__contention_test__.ovec"
#define DIM        4
#define ROW_COUNT  8

/* Event signalled by the releaser thread to wake the reader */
static HANDLE g_release_event = NULL;
/* Set to 1 once the reader has its mmap open */
static volatile LONG g_reader_ready = 0;

/* ── Reader thread: maps the file and holds it until told to release ── */
static DWORD WINAPI reader_fn(LPVOID arg)
{
    (void)arg;

    HANDLE hFile = CreateFileA(TEST_FILE, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[reader] CreateFileA failed: %lu\n", GetLastError());
        return 1;
    }
    HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMap) {
        fprintf(stderr, "[reader] CreateFileMappingA failed: %lu\n", GetLastError());
        CloseHandle(hFile);
        return 1;
    }
    void *addr = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!addr) {
        fprintf(stderr, "[reader] MapViewOfFile failed: %lu\n", GetLastError());
        CloseHandle(hMap);
        CloseHandle(hFile);
        return 1;
    }

    InterlockedExchange(&g_reader_ready, 1);         /* signal: mapping is live */
    WaitForSingleObject(g_release_event, INFINITE);  /* hold until released      */

    UnmapViewOfFile(addr);
    CloseHandle(hMap);
    CloseHandle(hFile);
    printf("[reader] mmap released\n");
    return 0;
}

/* ── Releaser thread: waits 80 ms then unblocks the reader ── */
static DWORD WINAPI releaser_fn(LPVOID arg)
{
    (void)arg;
    Sleep(80); /* let the writer's first MoveFileExA attempt fire and fail */
    SetEvent(g_release_event);
    printf("[releaser] fired — reader will drop mmap\n");
    return 0;
}

/* ── Helpers ── */
static int file_exists(const char *path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

/* ── Main ── */
int main(void)
{
    int passed = 1;

    /* Write the initial .ovec file */
    float data_v1[ROW_COUNT * DIM];
    for (int i = 0; i < ROW_COUNT * DIM; i++)
        data_v1[i] = (float)i * 0.5f;

    int rc = de_build_vectors_flat(data_v1, ROW_COUNT, DIM, TEST_FILE);
    if (rc != DE_OK) {
        fprintf(stderr, "FAIL: initial write returned %d (%s)\n", rc, de_strerror(rc));
        return 1;
    }
    printf("[main] Initial file written: %s\n", TEST_FILE);

    /* Create the release event (manual-reset, initially unsignalled) */
    g_release_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!g_release_event) {
        fprintf(stderr, "FAIL: CreateEvent: %lu\n", GetLastError());
        return 1;
    }

    /* Start reader: maps the file, signals g_reader_ready, then waits */
    HANDLE hReader = CreateThread(NULL, 0, reader_fn, NULL, 0, NULL);
    if (!hReader) {
        fprintf(stderr, "FAIL: CreateThread (reader): %lu\n", GetLastError());
        CloseHandle(g_release_event);
        return 1;
    }

    /* Wait for reader to have the file open */
    while (!InterlockedCompareExchange(&g_reader_ready, 0, 0))
        Sleep(1);
    printf("[main] Reader has file mmap'd — starting concurrent write\n");

    /* Start releaser: will drop reader's mmap after 80 ms */
    HANDLE hReleaser = CreateThread(NULL, 0, releaser_fn, NULL, 0, NULL);
    if (!hReleaser) {
        fprintf(stderr, "WARN: CreateThread (releaser) failed; test may not trigger contention\n");
        SetEvent(g_release_event); /* fall back: release immediately */
    }

    /* Overwrite the file — this should hit ERROR_SHARING_VIOLATION on the first
     * MoveFileExA attempt, then succeed on a subsequent retry after the reader
     * releases the mapping. */
    float data_v2[ROW_COUNT * DIM];
    for (int i = 0; i < ROW_COUNT * DIM; i++)
        data_v2[i] = (float)i * 2.0f;

    rc = de_build_vectors_flat(data_v2, ROW_COUNT, DIM, TEST_FILE);

    /* Collect threads */
    if (hReleaser) { WaitForSingleObject(hReleaser, 3000); CloseHandle(hReleaser); }
    WaitForSingleObject(hReader, 5000);
    CloseHandle(hReader);
    CloseHandle(g_release_event);

    /* --- Assertions --- */
    if (rc != DE_OK) {
        fprintf(stderr, "FAIL: de_build_vectors_flat under contention returned %d (%s)\n",
                rc, de_strerror(rc));
        passed = 0;
    } else {
        printf("[main] Write returned DE_OK\n");
    }

    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", TEST_FILE);
    if (file_exists(tmp_path)) {
        fprintf(stderr, "FAIL: leftover .tmp file found: %s\n", tmp_path);
        passed = 0;
    }

    if (passed) {
        table_t t;
        if (de_load(&t, TEST_FILE) != DE_OK) {
            fprintf(stderr, "FAIL: de_load of written file failed\n");
            passed = 0;
        } else {
            if ((uint32_t)t.row_count != ROW_COUNT) {
                fprintf(stderr, "FAIL: expected %d rows, got %d\n", ROW_COUNT, t.row_count);
                passed = 0;
            }
            uint32_t dim_out = 0;
            const float *vec0 = de_vector_get(&t, 0, &dim_out);
            if (!vec0 || dim_out != DIM) {
                fprintf(stderr, "FAIL: de_vector_get row 0 returned NULL or wrong dim\n");
                passed = 0;
            } else if (vec0[1] != data_v2[1]) {
                /* vec0[0] is 0.0f in both versions; use index 1 to distinguish */
                fprintf(stderr, "FAIL: vec0[1]=%.2f expected %.2f — stale data written\n",
                        vec0[1], data_v2[1]);
                passed = 0;
            }
            de_free(&t);
        }
    }

    /* Cleanup test artefacts */
    DeleteFileA(TEST_FILE);
    char lock_path[512];
    snprintf(lock_path, sizeof(lock_path), "%s.lock", TEST_FILE);
    DeleteFileA(lock_path);

    printf("%s: Windows mmap contention retry test\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}

#else /* !_WIN32 */

#include <stdio.h>
int main(void) {
    /* On POSIX, rename() over an mmap'd file succeeds immediately via inode
     * indirection — there is no sharing violation to retry. */
    printf("SKIP: Windows-only test\n");
    return 77; /* automake SKIP convention */
}

#endif /* _WIN32 */
