#include "ext.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

typedef struct WorkspaceLock {
    int state;
#if defined(_WIN32)
    HANDLE file;
#else
    int fd;
#endif
} WorkspaceLock;

static Str *workspace_string(const char *bytes, size_t length) {
    Str *s = calloc(1, sizeof(*s));
    if (!s) abort();
    s->c_str = calloc(length + 1, 1);
    if (!s->c_str) abort();
    if (length) memcpy(s->c_str, bytes, length);
    s->count = s->cap = (USize)length;
    return s;
}

void *workspace_lock_new(void) {
    WorkspaceLock *s = calloc(1, sizeof(*s));
    if (!s) abort();
#if defined(_WIN32)
    s->file = INVALID_HANDLE_VALUE;
#else
    s->fd = -1;
#endif
    return s;
}
static void workspace_unlock(WorkspaceLock *s) {
#if defined(_WIN32)
    if (s->file != INVALID_HANDLE_VALUE) CloseHandle(s->file);
    s->file = INVALID_HANDLE_VALUE;
#else
    if (s->fd >= 0) close(s->fd);
    s->fd = -1;
#endif
    s->state = 0;
}
int32_t workspace_lock_open(void *handle, const Str *path) {
    WorkspaceLock *s = handle;
    workspace_unlock(s);
#if defined(_WIN32)
    s->file = CreateFileA(path->c_str, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (s->file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        return error == ERROR_SHARING_VIOLATION ? 0 : -(int32_t)error;
    }
#else
    s->fd = open(path->c_str, O_CREAT | O_RDWR, 0600);
    if (s->fd < 0) return -errno;
    if (fcntl(s->fd, F_SETFD, FD_CLOEXEC) != 0) {
        int error = errno;
        workspace_unlock(s);
        return -error;
    }
    if (flock(s->fd, LOCK_EX | LOCK_NB) != 0) {
        int busy = errno == EAGAIN || errno == EWOULDBLOCK;
        int error = errno;
        workspace_unlock(s);
        return busy ? 0 : -error;
    }
#endif
    s->state = 1;
    return 1;
}
void workspace_lock_free(void *handle) {
    WorkspaceLock *s = handle;
    workspace_unlock(s);
    free(s);
}

// Errors are returned to the host, never fatal to an editing session. -1 means
// absent; other errors retain errno-style codes. Bound input before allocation.
Str *workspace_read(const Str *path, int32_t *error) {
    *error = 0;
    FILE *f = fopen(path->c_str, "rb");
    if (!f) { *error = errno == ENOENT ? -1 : errno; return workspace_string("", 0); }
    if (fseek(f, 0, SEEK_END) != 0) { *error = EIO; fclose(f); return workspace_string("", 0); }
    long length = ftell(f);
    if (length < 0 || length > 64 * 1024 * 1024) {
        *error = EFBIG; fclose(f); return workspace_string("", 0);
    }
    rewind(f);
    Str *out = workspace_string("", 0);
    free(out->c_str);
    out->c_str = calloc((size_t)length + 1, 1);
    if (!out->c_str) abort();
    out->count = out->cap = (USize)length;
    if (fread(out->c_str, 1, (size_t)length, f) != (size_t)length) *error = EIO;
    if (fclose(f) != 0) *error = EIO;
    return out;
}
int64_t workspace_hash(const Str *text) {
    uint64_t h = UINT64_C(14695981039346656037);
    for (USize i = 0; i < text->count; ++i) h = (h ^ (unsigned char)text->c_str[i]) * UINT64_C(1099511628211);
    return (int64_t)(h & INT64_MAX);
}

static int workspace_rename(const char *from, const char *to) {
#if defined(_WIN32)
    if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 0;
    return GetLastError() == ERROR_FILE_NOT_FOUND ? ENOENT : EIO;
#else
    return rename(from, to) == 0 ? 0 : errno;
#endif
}
int32_t workspace_write(void *handle, const Str *path, const Str *bytes, Bool rotate) {
    WorkspaceLock *s = handle;
    if (s->state != 1) return EPERM;
    size_t n = strlen(path->c_str);
    char *temporary = malloc(n + 16), *previous = malloc(n + 16);
    if (!temporary || !previous) abort();
    snprintf(temporary, n + 16, "%s.tmp", path->c_str);
    snprintf(previous, n + 16, "%s.previous", path->c_str);
    int error = 0;
#if defined(_WIN32)
    FILE *f = fopen(temporary, "wb");
#else
    int fd = open(temporary, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    FILE *f = fd < 0 ? NULL : fdopen(fd, "wb");
    if (!f && fd >= 0) close(fd);
#endif
    if (!f) error = errno ? errno : EIO;
    if (f) {
        if (fwrite(bytes->c_str, 1, bytes->count, f) != bytes->count || fflush(f) != 0) error = EIO;
#if defined(_WIN32)
        if (!error && _commit(_fileno(f)) != 0) error = EIO;
#else
        if (!error && fsync(fileno(f)) != 0) error = errno;
#endif
        if (fclose(f) != 0 && !error) error = EIO;
    }
    if (!error && rotate) {
        error = workspace_rename(path->c_str, previous);
        if (error == ENOENT) error = 0; // Previous generation still protects a failed publish.
    }
    if (!error) error = workspace_rename(temporary, path->c_str);
#if !defined(_WIN32)
    if (!error) {
        char *parent = strdup(path->c_str);
        if (!parent) abort();
        char *slash = strrchr(parent, '/');
        if (slash) *slash = 0;
        int dir = open(slash ? parent : ".", O_RDONLY);
        if (dir < 0 || fsync(dir) != 0) error = errno;
        if (dir >= 0) close(dir);
        free(parent);
    }
#endif
    if (error) remove(temporary);
    free(temporary); free(previous);
    return error;
}
