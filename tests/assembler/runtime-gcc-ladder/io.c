/* Stage 5: real libc buffered file I/O, seek, readback, and cleanup. */
#include <stdio.h>
#include <string.h>

int main(void) {
    static const unsigned char payload[] = "abcdefghij";
    unsigned char readback[sizeof(payload) - 1U];
    FILE *file = tmpfile();
    if (file == NULL)
        return 31;
    size_t length = sizeof(payload) - 1U;
    int ok = fwrite(payload, 1U, length, file) == length &&
             fflush(file) == 0 && fseek(file, 0L, SEEK_SET) == 0 &&
             fread(readback, 1U, length, file) == length;
    unsigned sum = 0U;
    if (ok) {
        for (size_t i = 0U; i < length; ++i)
            sum += readback[i];
        ok = sum == 1015U && memcmp(payload, readback, length) == 0;
    }
    ok = fclose(file) == 0 && ok;
    printf("LADDER_IO sum=%u status=%s\n", sum, ok ? "PASS" : "FAIL");
    return ok ? 0 : 32;
}
