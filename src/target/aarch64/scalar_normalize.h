#ifndef MINIC_TARGET_AARCH64_SCALAR_NORMALIZE_H
#define MINIC_TARGET_AARCH64_SCALAR_NORMALIZE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* One native normalization implementation shared by FP, integer, ABI,
 * and residual machine families; never a Core semantic width classifier. */
bool minic_aarch64_emit_integer_normalize(FILE *, size_t, bool);
#endif
