/* A machine-only translation unit must not inherit the C expression and
 * declaration graph when it asks for target layout, frame or Core emission. */
#include "target/backend.h"
#include "target/target_info.h"
#include "target/data_layout.h"
#include "target/common/linux_jump_label_asm.h"
#include "target/common/lp64_frame.h"
#include "target/riscv32/rv32_emit.h"
#include "target/riscv32/rv32_internal.h"
#include "target/riscv64/codegen.h"
#include "target/x86_64/asm_contract.h"
#include "target/x86_64/codegen.h"
#include "target/x86_64/call_plan.h"
#include "target/x86_64/frame.h"
#include "target/aarch64/codegen.h"
#include "target/aarch64/asm_contract.h"
#include "target/aarch64/block_emit.h"
#include "target/loongarch64/codegen.h"
#include "target/loongarch64/asm_contract.h"
#include "target/loongarch64/block_emit.h"

#ifdef MINIC_FRONTEND_AST_H
#error "machine/Core target headers leaked the full C AST"
#endif
#ifdef MINIC_FRONTEND_TYPE_H
#error "machine/Core target headers leaked the C source type model"
#endif

/* A complete machine-facing callback contract is visible without the AST. */
static MinicBackendEmitModule machine_emitter_type;

int main(void) {
    return machine_emitter_type != NULL;
}
