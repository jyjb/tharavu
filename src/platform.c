#include "../include/data_engine.h"
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#define F_GETLK 0
#define F_SETLK 0
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#endif

/* --- Endianness Helpers (Little Endian Standard) --- */
uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

void write_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | 
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void write_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

uint64_t read_u64_le(const uint8_t *p) {
    uint64_t v = 0;
    for(int i=0; i<8; i++) v |= ((uint64_t)p[i] << (i*8));
    return v;
}

void write_u64_le(uint8_t *p, uint64_t v) {
    for(int i=0; i<8; i++) p[i] = (uint8_t)(v >> (i*8));
}

/* --- Platform MMAP & Locking --- */

int de_platform_lock(int fd, int exclusive) {
#ifdef _WIN32
    OVERLAPPED ov = {0};
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) return DE_ERR_IO;
    
    DWORD flags = exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;
    if (!LockFileEx(h, flags, 0, 1, 0, &ov)) {
        return DE_ERR_LOCK;
    }
    return DE_OK;
#else
    struct flock fl;
    fl.l_type = exclusive ? F_WRLCK : F_RDLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 1; // Lock first byte
    
    if (fcntl(fd, F_SETLKW, &fl) == -1) {
        return DE_ERR_LOCK;
    }
    return DE_OK;
#endif
}

int de_platform_unlock(int fd) {
#ifdef _WIN32
    OVERLAPPED ov = {0};
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) return DE_ERR_IO;
    if (!UnlockFileEx(h, 0, 1, 0, &ov)) return DE_ERR_IO;
#else
    struct flock fl = {F_UNLCK, SEEK_SET, 0, 1, 0};
    if (fcntl(fd, F_SETLK, &fl) == -1) return DE_ERR_IO;
#endif
    return DE_OK;
}

int de_platform_mmap_readonly(const char *path, void **addr, size_t *len, int *fd_out) {
#ifdef _WIN32
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return DE_ERR_IO;
    
    LARGE_INTEGER liSize;
    if (!GetFileSizeEx(hFile, &liSize)) { CloseHandle(hFile); return DE_ERR_IO; }
    *len = (size_t)liSize.QuadPart;
    
    HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMap) { CloseHandle(hFile); return DE_ERR_IO; }
    
    *addr = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(hMap);
    CloseHandle(hFile); // Keep mapping alive, close handle
    
    if (!*addr) return DE_ERR_IO;
    *fd_out = -1; // Windows handle not needed for unmapping
    return DE_OK;
#else
    int fd = open(path, O_RDONLY);
    if (fd == -1) return DE_ERR_IO;
    
    struct stat sb;
    if (fstat(fd, &sb) == -1) { close(fd); return DE_ERR_IO; }
    *len = sb.st_size;
    
    *addr = mmap(NULL, *len, PROT_READ, MAP_PRIVATE, fd, 0);
    if (*addr == MAP_FAILED) { close(fd); return DE_ERR_IO; }
    
    *fd_out = fd;
    return DE_OK;
#endif
}

void de_platform_unmap(void *addr, size_t len, int fd) {
    if (!addr) return;
#ifdef _WIN32
    (void)len;
    (void)fd;
    UnmapViewOfFile(addr);
#else
    munmap(addr, len);
    if (fd != -1) close(fd);
#endif
}

/* Helper to read safely from mmap or file */
int de_safe_read(void *base, size_t base_size, uint64_t offset, void *dst, size_t count) {
    if (!base || !dst) return DE_ERR_CORRUPT;
    if (offset > base_size || count > base_size - offset) return DE_ERR_CORRUPT;
    memcpy(dst, (uint8_t*)base + offset, count);
    return DE_OK;
}

/* --- Sidecar lock file helpers --- */
/*
 * Opens (or creates) a sidecar .lock file used to serialise writes.
 * We use a separate file rather than locking the target file so that
 * the target can be renamed (atomic save) while the lock is held.
 * Returns the file descriptor on success, -1 on failure.
 */
int de_platform_open_for_lock(const char *path) {
#ifdef _WIN32
    int fd = _open(path, _O_CREAT | _O_RDWR, _S_IREAD | _S_IWRITE);
#else
    int fd = open(path, O_CREAT | O_RDWR, 0600);
#endif
    return fd; /* -1 on failure */
}

void de_platform_close_fd(int fd) {
    if (fd < 0) return;
#ifdef _WIN32
    _close(fd);
#else
    close(fd);
#endif
}