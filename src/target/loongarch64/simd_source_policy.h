#ifndef MINIC_TARGET_LOONGARCH64_SIMD_SOURCE_POLICY_H
#define MINIC_TARGET_LOONGARCH64_SIMD_SOURCE_POLICY_H

#include "target/backend_source.h"
#include "frontend/ast.h"

/* GNU C source acceptance ends at Core ingress.  Machine SIMD emission must
 * never consult these mutable Source Program or C type descriptors. */
bool minic_loongarch64_simd_qualify_source(
    const MinicC0Program *, const MinicInlineAsm *,
    MinicTargetInlineAsmContract *);
bool minic_loongarch64_simd_seal_normalized(
    const MinicTargetInvocation *, const MinicC0Program *, const MinicInlineAsm *,
    const char *, size_t, MinicTargetInlineAsmContract *);

#endif
