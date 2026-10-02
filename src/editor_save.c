#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Til's current File.writefile exits on an open error. An editor must keep
// unsaved buffers alive when saving fails, including a delayed fclose error.
int32_t editor_write_file(const char *path, const char *text, uintptr_t length)
{
    FILE *file = fopen(path, "wb");
    if (!file) return errno ? errno : EIO;
    int error = 0;
    if (fwrite(text, 1, (size_t)length, file) != (size_t)length)
        error = errno ? errno : EIO;
    if (fclose(file) != 0 && !error) error = errno ? errno : EIO;
    return error;
}

const char *editor_write_error(int32_t error)
{
    return strerror(error);
}
