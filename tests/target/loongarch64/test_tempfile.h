#ifndef MINIC_LOONG_TEST_TEMPFILE_H
#define MINIC_LOONG_TEST_TEMPFILE_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* glibc tmpfile() uses /tmp even when TMPDIR designates a task-owned
 * workspace. Preserve seekable read/write stream tests without depending
 * on another task's temporary filesystem capacity. */
static inline FILE *loong_test_tempfile(void) {
    const char *directory = getenv("TMPDIR");
    char path[1024];
    int fd, length;
    FILE *stream;
    if (directory == NULL || directory[0] == '\0') return tmpfile();
    length = snprintf(path, sizeof(path), "%s/minic-loong-XXXXXX", directory);
    if (length < 0 || (size_t)length >= sizeof(path)) return NULL;
    fd = mkstemp(path);
    if (fd < 0) return NULL;
    (void)unlink(path);
    stream = fdopen(fd, "w+");
    if (stream == NULL) (void)close(fd);
    return stream;
}

#endif
