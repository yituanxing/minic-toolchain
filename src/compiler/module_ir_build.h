#ifndef MINIC_COMPILER_MODULE_IR_BUILD_H
#define MINIC_COMPILER_MODULE_IR_BUILD_H

/* C -> Core Module ingress. This compiler-only adapter is never included
 * from a Core verifier or target emitter. */
#include "core/module_ir.h"
#include "compiler/c_lower/core_type_import.h"
#include "target/source_policy/source_info.h"

/* Both stages operate on the same ModuleIR. The first owns all symbol/root
 * facts without serializing dead global storage; after the Core/Module graph
 * closes, the second imports only live payloads and seals the module. */
bool minic_core_module_prepare_from_program(MinicCoreModule *output,
                                            const MinicC0Program *program,
                                            MinicCoreTypeImporter *importer);
bool minic_core_module_materialize_live_payloads(MinicCoreModule *module,
                                                  const MinicC0Program *program,
                                                  const MinicTargetInvocation *target,
                                                  MinicCoreTypeImporter *importer);

#endif
