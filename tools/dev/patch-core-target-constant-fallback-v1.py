from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected exactly one anchor in {path}, found {count}")
    file.write_text(text.replace(old, new, 1))


# Core lowering consumes target semantic layout, while Core IR remains target-neutral.
replace_once(
    "src/core/core_lower.h",
    '#include "frontend/function_body.h"\n',
    '#include "frontend/function_body.h"\n#include "target/target_info.h"\n',
)
replace_once(
    "src/core/core_lower.h",
    "MinicCoreLowerStatus minic_core_lower_function(const MinicFunctionBodyView *body,\n"
    "                                               MinicCoreFunction *output);",
    "MinicCoreLowerStatus minic_core_lower_function(const MinicFunctionBodyView *body,\n"
    "                                               const MinicTargetInfo *target,\n"
    "                                               MinicCoreFunction *output);",
)

replace_once(
    "src/core/core_lower.c",
    '#include "frontend/expression_semantics.h"\n',
    '#include "frontend/const_eval.h"\n#include "frontend/expression_semantics.h"\n',
)
replace_once(
    "src/core/core_lower.c",
    "    const MinicFunction *source_function;\n"
    "    MinicCoreFunction *function;",
    "    const MinicFunction *source_function;\n"
    "    const MinicTargetInfo *target;\n"
    "    MinicCoreFunction *function;",
)

# Existing supported execution operations keep priority. Only otherwise-unsupported
# pure integer rvalues may collapse through frontend const-eval.
replace_once(
    "src/core/core_lower.c",
    "    return MINIC_CORE_LOWER_UNSUPPORTED;\n}\n\nstatic MinicCoreLowerStatus lower_assignment_pair",
    "    if (minic_type_is_integer(expression->type) && context->target != NULL) {\n"
    "        MinicConstValue constant;\n"
    "        uint64_t constant_bits;\n\n"
    "        if (minic_const_eval_integer(\n"
    "                context->body->program, context->target, expression_id, &constant) &&\n"
    "            minic_type_equal(constant.type, expression->type)) {\n"
    "            (void)memset(&instruction, 0, sizeof(instruction));\n"
    "            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;\n"
    "            instruction.span = expression->span;\n"
    "            instruction.type = expression->type;\n"
    "            instruction.result = MINIC_CORE_VALUE_INVALID;\n"
    "            constant_bits = constant.bits;\n"
    "            (void)memcpy(&instruction.value.integer_value,\n"
    "                         &constant_bits,\n"
    "                         sizeof(instruction.value.integer_value));\n"
    "            return minic_core_function_append_value_instruction(\n"
    "                       context->function, context->block_id, &instruction, value_id)\n"
    "                       ? MINIC_CORE_LOWER_OK\n"
    "                       : MINIC_CORE_LOWER_ERROR;\n"
    "        }\n"
    "    }\n"
    "    return MINIC_CORE_LOWER_UNSUPPORTED;\n}\n\nstatic MinicCoreLowerStatus lower_assignment_pair",
)

replace_once(
    "src/core/core_lower.c",
    "MinicCoreLowerStatus minic_core_lower_function(const MinicFunctionBodyView *body,\n"
    "                                               MinicCoreFunction *output) {",
    "MinicCoreLowerStatus minic_core_lower_function(const MinicFunctionBodyView *body,\n"
    "                                               const MinicTargetInfo *target,\n"
    "                                               MinicCoreFunction *output) {",
)
replace_once(
    "src/core/core_lower.c",
    "    if (body == NULL || body->program == NULL || output == NULL) {",
    "    if (body == NULL || body->program == NULL || target == NULL || output == NULL) {",
)
replace_once(
    "src/core/core_lower.c",
    "    context.source_function = source_function;\n"
    "    context.function = &lowered;",
    "    context.source_function = source_function;\n"
    "    context.target = target;\n"
    "    context.function = &lowered;",
)

# Compiler owns the selected target and passes it explicitly into AST -> Core lowering.
replace_once(
    "src/compiler/compiler.c",
    "static bool minic_prepare_core_candidates(const MinicC0Program *program,\n"
    "                                          MinicCoreCandidates *output) {",
    "static bool minic_prepare_core_candidates(const MinicC0Program *program,\n"
    "                                          const MinicTargetInfo *target,\n"
    "                                          MinicCoreCandidates *output) {",
)
replace_once(
    "src/compiler/compiler.c",
    "    if (program == NULL || output == NULL ||",
    "    if (program == NULL || target == NULL || output == NULL ||",
)
replace_once(
    "src/compiler/compiler.c",
    "            minic_core_lower_function(&body, &candidates.functions[function_index]);",
    "            minic_core_lower_function(&body, target, &candidates.functions[function_index]);",
)
replace_once(
    "src/compiler/compiler.c",
    "        !minic_prepare_core_candidates(&program, &core_candidates)) {",
    "        !minic_prepare_core_candidates(&program, target_info, &core_candidates)) {",
)

# Direct unit-test callers use the same default TargetInfo as the compiler.
path = Path("tests/core/core_ir_shadow_test.c")
text = path.read_text()
if '#include "target/target_info.h"\n' not in text:
    text = text.replace('#include "frontend/function_body.h"\n',
                        '#include "frontend/function_body.h"\n#include "target/target_info.h"\n', 1)
count = text.count("minic_core_lower_function(&view, &core)")
if count == 0:
    raise SystemExit("no core shadow lower_function callers found")
text = text.replace("minic_core_lower_function(&view, &core)",
                    "minic_core_lower_function(&view, minic_default_target_info(), &core)")
path.write_text(text)

replace_once(
    "tests/target/riscv64/core_frontend_emitter_test.c",
    "        status = minic_core_lower_function(&body, &core);",
    "        status = minic_core_lower_function(&body, minic_default_target_info(), &core);",
)

# Focused source: direct sizeof plus the actual WRITE_ONCE compiletime-assert shape.
Path("tests/compiler/c0/core_target_constant_fallback.c").write_text("""struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

unsigned long core_m10_pointer_size(void) {
    return sizeof(void *);
}

void core_m10_init_list(struct list_head *list) {
    do {
        do {
            __attribute__((noreturn, error("Unsupported access size for WRITE_ONCE")))
            extern void core_m10_compiletime_error_next(void);
            if (!((sizeof(list->next) == sizeof(char) ||
                   sizeof(list->next) == sizeof(short) ||
                   sizeof(list->next) == sizeof(int) ||
                   sizeof(list->next) == sizeof(long)) ||
                  sizeof(list->next) == sizeof(long long)))
                core_m10_compiletime_error_next();
        } while (0);
        do {
            *(volatile __typeof__(list->next) *)&(list->next) = list;
        } while (0);
    } while (0);
    do {
        do {
            __attribute__((noreturn, error("Unsupported access size for WRITE_ONCE")))
            extern void core_m10_compiletime_error_prev(void);
            if (!((sizeof(list->prev) == sizeof(char) ||
                   sizeof(list->prev) == sizeof(short) ||
                   sizeof(list->prev) == sizeof(int) ||
                   sizeof(list->prev) == sizeof(long)) ||
                  sizeof(list->prev) == sizeof(long long)))
                core_m10_compiletime_error_prev();
        } while (0);
        do {
            *(volatile __typeof__(list->prev) *)&(list->prev) = list;
        } while (0);
    } while (0);
}
""")

Path("tests/compiler/c0/core_target_constant_fallback_runtime.c").write_text("""#include <stdio.h>

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

unsigned long core_m10_pointer_size(void);
void core_m10_init_list(struct list_head *list);

void core_m10_compiletime_error_next(void) {
    puts("BAD-next");
}

void core_m10_compiletime_error_prev(void) {
    puts("BAD-prev");
}

int main(void) {
    struct list_head list = { 0, 0 };
    core_m10_init_list(&list);
    printf("%lu %d %d\\n",
           core_m10_pointer_size(),
           list.next == &list,
           list.prev == &list);
    return 0;
}
""")

Path("tests/compiler/c0/run-core-target-constant-fallback.sh").write_text("""#!/bin/sh
set -eu
: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-target-constant-fallback}"
source_file="$root/tests/compiler/c0/core_target_constant_fallback.c"
runtime_file="$root/tests/compiler/c0/core_target_constant_fallback_runtime.c"
mkdir -p "$work"
cc -E -P -std=gnu11 "$source_file" -o "$work/input.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/input.i" -o "$work/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/input.i" -o "$work/core.s"
grep -q '^core_m10_pointer_size:' "$work/core.s"
grep -q '^core_m10_init_list:' "$work/core.s"
"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core.s" -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\\n' 'PASS compiler/c0/core-target-constant-fallback'
""")

# Permanent C0 gate.
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "core_integer_bitwise_not_focused() {",
    "core_target_constant_fallback_focused() {\n"
    "    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n"
    "    BUILD_DIR=\"$root/build/ci-core-target-constant-fallback\" \\\n"
    "    RISCV_CC=riscv64-linux-gnu-gcc \\\n"
    "    QEMU_RISCV64=qemu-riscv64 \\\n"
    "        sh tests/compiler/c0/run-core-target-constant-fallback.sh\n"
    "}\n\n"
    "core_integer_bitwise_not_focused() {",
)
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "start_gate core-integer-subtract-overflow-focused core_integer_subtract_overflow_focused\n"
    "start_gate core-integer-bitwise-not-focused core_integer_bitwise_not_focused",
    "start_gate core-integer-subtract-overflow-focused core_integer_subtract_overflow_focused\n"
    "start_gate core-target-constant-fallback-focused core_target_constant_fallback_focused\n"
    "start_gate core-integer-bitwise-not-focused core_integer_bitwise_not_focused",
)
