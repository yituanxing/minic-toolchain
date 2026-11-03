#ifndef MINIC_TARGET_LOONGARCH64_MACHINE_TEXT_OPT_H
#define MINIC_TARGET_LOONGARCH64_MACHINE_TEXT_OPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Two target-owned, ordered passes over compiler-generated GNU LoongArch64
 * assembly. The single-use pass can consult an immutable Core use census;
 * compaction afterward removes private markers, redundant scalar reloads
 * and proven branches. Both preserve opaque inline-ASM barriers. */
bool minic_loongarch64_forward_single_use_spills(FILE *out,
    const char *buffer, size_t length, const size_t *uses,
    size_t value_count);
bool minic_loongarch64_emit_compact_assembly(FILE *out,
    const char *buffer, size_t buffer_length);

#endif
