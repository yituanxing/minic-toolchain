#ifndef MINIC_TARGET_COMMON_ELF_MODULE_SYMBOL_INFO_H
#define MINIC_TARGET_COMMON_ELF_MODULE_SYMBOL_INFO_H

#include "core/module_ir.h"

/* ELF linkage is an object-format decision over a sealed Module, not a
 * machine TargetInfo capability and never a late Source AST lookup. */
typedef enum MinicElfModuleSymbolKind {
    MINIC_ELF_MODULE_SYMBOL_OBJECT = 0,
    MINIC_ELF_MODULE_SYMBOL_FUNCTION
} MinicElfModuleSymbolKind;

typedef struct MinicElfModuleSymbolFacts {
    bool tu_local;
    bool nonpreemptible;
    bool undefined_weak;
} MinicElfModuleSymbolFacts;

/* All flags are false for malformed/unfinalized modules and unknown names.
 * A real definition or an emitted alias takes precedence over an earlier
 * same-spelled extern-only weak declaration. Other visibility declarations
 * can still constrain the same ELF symbol's preemptibility. */
MinicElfModuleSymbolFacts minic_multi_elf_module_symbol_facts(
    const MinicCoreModule *module, const char *symbol,
    MinicElfModuleSymbolKind kind);

#endif
