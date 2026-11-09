#ifndef MINIC_TARGET_COMMON_CORE_EXTENDED_FLOAT_RUNTIME_H
#define MINIC_TARGET_COMMON_CORE_EXTENDED_FLOAT_RUNTIME_H

#include "core/core_ir.h"

/* Binary128 TFmode's public compiler-runtime names. The caller may select a
 * native x87 implementation instead; these names never define Core identity.
 * In particular, GNU __eqtf2/__lttf2/__letf2 return comparison statuses,
 * while the corresponding Core operation returns an ordered 0/1 predicate:
 * each machine target must normalize the helper result at the call site. */
static inline const char *minic_core_extended_float_runtime_symbol(
    MinicCoreExtendedFloatOperation operation) {
    switch (operation) {
    case MINIC_CORE_EXT_FLOAT_ADD: return "__addtf3";
    case MINIC_CORE_EXT_FLOAT_SUBTRACT: return "__subtf3";
    case MINIC_CORE_EXT_FLOAT_MULTIPLY: return "__multf3";
    case MINIC_CORE_EXT_FLOAT_DIVIDE: return "__divtf3";
    case MINIC_CORE_EXT_FLOAT_NEGATE: return "__negtf2";
    case MINIC_CORE_EXT_FLOAT_EQUAL: return "__eqtf2";
    case MINIC_CORE_EXT_FLOAT_LESS: return "__lttf2";
    case MINIC_CORE_EXT_FLOAT_LESS_EQUAL: return "__letf2";
    case MINIC_CORE_EXT_FLOAT_FROM_BINARY32: return "__extendsftf2";
    case MINIC_CORE_EXT_FLOAT_FROM_BINARY64: return "__extenddftf2";
    case MINIC_CORE_EXT_FLOAT_TO_BINARY32: return "__trunctfsf2";
    case MINIC_CORE_EXT_FLOAT_TO_BINARY64: return "__trunctfdf2";
    case MINIC_CORE_EXT_FLOAT_FROM_SIGNED32: return "__floatsitf";
    case MINIC_CORE_EXT_FLOAT_FROM_UNSIGNED32: return "__floatunsitf";
    case MINIC_CORE_EXT_FLOAT_FROM_SIGNED64: return "__floatditf";
    case MINIC_CORE_EXT_FLOAT_FROM_UNSIGNED64: return "__floatunditf";
    case MINIC_CORE_EXT_FLOAT_TO_SIGNED32: return "__fixtfsi";
    case MINIC_CORE_EXT_FLOAT_TO_UNSIGNED32: return "__fixunstfsi";
    case MINIC_CORE_EXT_FLOAT_TO_SIGNED64: return "__fixtfdi";
    case MINIC_CORE_EXT_FLOAT_TO_UNSIGNED64: return "__fixunstfdi";
    default: return NULL;
    }
}

#endif
