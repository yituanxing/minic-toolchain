#include "riscv_elf_abi.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void expect_merge(uint32_t old_flags, uint32_t new_flags,
                         bool initialized, bool incoming_has_code,
                         uint32_t expected_flags) {
    uint32_t merged = UINT32_MAX;
    const char *reason = "uninitialized";

    assert(minild_riscv_elf_abi_merge(old_flags, new_flags, initialized,
                                     incoming_has_code,
                                     &merged, &reason));
    assert(reason == NULL);
    assert(merged == expected_flags);
}

static void expect_reject(uint32_t old_flags, uint32_t new_flags,
                          const char *expected_reason) {
    uint32_t merged = UINT32_MAX;
    const char *reason = NULL;

    assert(!minild_riscv_elf_abi_merge(old_flags, new_flags, true, true,
                                      &merged, &reason));
    assert(reason != NULL && strcmp(reason, expected_reason) == 0);
    assert(merged == UINT32_MAX);
}

int main(void) {
    /* RVC and TSO are union-compatible, while FLOAT ABI is exclusive. */
    expect_merge(0U, 4U, false, true, 4U);
    expect_merge(4U, 5U, true, true, 5U);
    expect_merge(5U, 4U, true, true, 5U);
    expect_merge(0U, 0U, true, true, 0U);
    expect_merge(4U | 0x20U, 4U | 0x21U, true, true, 4U | 0x21U);
    expect_merge(0x10U, 0U, true, true, 0x10U);
    expect_merge(0U, 0x10U, true, true, 0x10U);
    /* GNU BFD explicitly ignores code-ABI differences in data-only inputs. */
    expect_merge(4U, 2U, true, false, 4U);

    /* An OR of LP64F (2) and LP64D (4) would incorrectly advertise Q (6). */
    expect_reject(2U, 4U, "float-abi-mismatch");
    expect_reject(0U, 2U, "float-abi-mismatch");
    expect_reject(8U, 0U, "embedded-abi-mismatch");
    expect_reject(0x20U, 0U, "processor-elf-flags-mismatch");

    puts("MINILD_RISCV_ABI=PASS compatible=8 rejected=4");
    return 0;
}
