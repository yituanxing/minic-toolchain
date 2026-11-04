#ifndef MINIC_TARGET_LOONGARCH64_MACHINE_TEXT_OPT_H
#define MINIC_TARGET_LOONGARCH64_MACHINE_TEXT_OPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Target-owned SSA forwarding and machine text compaction. The production
 * path streams the forwarder's lines into a stateful compactor (one whole
 * function text buffer); both historical stage APIs remain available for
 * exact two-pass/fused differential tests. GNU inline ASM stays opaque. */
bool minic_loongarch64_forward_single_use_spills(FILE *out,
    const char *buffer, size_t length, const size_t *uses,
    size_t value_count);
bool minic_loongarch64_emit_compact_assembly(FILE *out,
    const char *buffer, size_t buffer_length);
bool minic_loongarch64_forward_and_compact(FILE *out,
    const char *buffer, size_t length, const size_t *uses,
    size_t value_count);

#endif
