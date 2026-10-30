#include "target/riscv/record_copy_bytes.h"
/* Byte-exact forward copy (not memmove). RECORD_LOAD and RECORD_COPY previously
 * contained independent copies of this 2048-byte loop with reversed roles.
 * Register contract: t2 is scratch when size > 0, t3 when size > 2048; for
 * multi-chunk copies both source/destination GPRs are advanced in place.
 * Callers must not assume these registers retain their incoming addresses. */
bool minic_riscv_emit_record_copy_bytes(FILE *file,
                                   const char *destination_register,
                                   const char *source_register,
                                   size_t size) {
    size_t copied = 0U;
    if (file == NULL || destination_register == NULL || source_register == NULL)
        return false;
    while (copied < size) {
        size_t chunk = size - copied;
        size_t offset;
        if (chunk > 2048U) chunk = 2048U;
        for (offset = 0U; offset < chunk; ++offset) {
            if (fprintf(file,
                        "  lbu t2, %zu(%s)\n"
                        "  sb t2, %zu(%s)\n",
                        offset, source_register, offset, destination_register) < 0)
                return false;
        }
        copied += chunk;
        if (copied < size &&
            fprintf(file,
                    "  li t3, %zu\n"
                    "  add %s, %s, t3\n"
                    "  add %s, %s, t3\n",
                    chunk, destination_register, destination_register,
                    source_register, source_register) < 0)
            return false;
    }
    return true;
}
