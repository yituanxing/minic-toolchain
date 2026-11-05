/* Authentic GNU-GCC RV32I local-exec TLS access. The existing Linux
 * Picolibc entry constructs tp from ELF PT_TLS before calling main(). */
#include <errno.h>
#include <stdio.h>

__thread int rv32_tls_init = 74;
__thread int rv32_tls_zero;

int main(void) {
    int *volatile errno_slot = &errno;
    rv32_tls_zero = 11;
    *errno_slot = 19;
    if (rv32_tls_init != 74 || rv32_tls_zero != 11 || errno != 19)
        return 96;
    printf("MINILD_RV32_TLS init=%d zero=%d errno=%d status=PASS\n",
           rv32_tls_init, rv32_tls_zero, errno);
    return 0;
}
