#ifndef MINIC_COMPILER_MODULE_IR_BUILD_H
#define MINIC_COMPILER_MODULE_IR_BUILD_H

/* C -> Core Module ingress. This compiler-only adapter is never included
 * from a Core verifier or target emitter. */
#include "core/module_ir.h"
#include "compiler/c_lower/core_type_import.h"
#include "target/source_policy/source_info.h"

bool minic_core_module_build_from_program(MinicCoreModule *output,
                                          const MinicC0Program *program,
                                          const MinicTargetInvocation *target,
                                          MinicCoreTypeImporter *importer);

#endif
