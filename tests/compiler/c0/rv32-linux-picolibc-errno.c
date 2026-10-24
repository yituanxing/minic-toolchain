/* Target libc ABI integration, not a replacement errno implementation:
 * the real Picolibc TLS errno symbol is accessed by GNU GCC-generated
 * thread pointer code and exposed through the documented errno-function
 * declaration mode in Picolibc's public errno.h. */
#include <errno.h>

int *__minic_picolibc_errno(void) {
    return &errno;
}
