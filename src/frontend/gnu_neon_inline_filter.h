#ifndef MINIC_FRONTEND_GNU_NEON_INLINE_FILTER_H
#define MINIC_FRONTEND_GNU_NEON_INLINE_FILTER_H

#include "target/source_policy/source_info.h"

/* An optional, target-sealed source prepass. The returned buffer is owned by
 * the caller, and every byte offset/newline of the original TU is retained.
 * A NULL buffer means the original input must be parsed unchanged. */
bool minic_gnu_neon_inline_filter(const char *path, const char *source,
                                  size_t length, const MinicTargetInvocation *target,
                                  char **filtered);

#endif
