/* The standalone Core reachability API must retain the original raw-ASM
 * matcher semantics with indexed token lookup: exact identifier boundaries,
 * duplicate Module names, comments, mnemonic collisions and unusual names. */
#include "compiler/core_reachability.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MAX_NAMES 8U

static bool run_case(const char *label, const char *template_text,
                     size_t template_length,
                     bool target_qualified,
                     const char *const *function_names, const bool *function_live,
                     size_t function_count,
                     const char *const *object_names, const bool *object_live,
                     size_t object_count) {
    MinicCoreModuleFunctionSymbol symbols[MAX_NAMES] = {{0}};
    MinicCoreModuleGlobal globals[MAX_NAMES] = {{0}};
    MinicCoreFunction functions[MAX_NAMES] = {{0}};
    MinicCoreLowerStatus statuses[MAX_NAMES] = {0};
    MinicCoreInlineAsm assembly = {0};
    MinicCoreInstruction instruction = {0};
    MinicCoreInstructionId instruction_id = 0U;
    MinicCoreBlock block = {0};
    MinicCoreFunctionSet set = {0};
    size_t i;
    if (function_count == 0U || function_count > MAX_NAMES ||
        object_count > MAX_NAMES) return false;
    for (i = 0U; i < function_count; ++i) {
        symbols[i].name = (char *)function_names[i];
        symbols[i].is_defined = true;
        symbols[i].is_internal = true;
        symbols[i].is_reachability_root = i == 0U;
        symbols[i].alias_target_index = SIZE_MAX;
        symbols[i].storage_owner_function_index = SIZE_MAX;
        statuses[i] = MINIC_CORE_LOWER_OK;
    }
    for (i = 0U; i < object_count; ++i) {
        globals[i].name = (char *)object_names[i];
        globals[i].is_internal = true;
        globals[i].emittable = true;
        globals[i].definition_kind = MINIC_CORE_MODULE_DEFINITION_REGULAR;
        globals[i].owner_function_index = SIZE_MAX;
        globals[i].alias_target_index = SIZE_MAX;
    }
    assembly.template_text = (char *)template_text;
    assembly.template_length = template_length == 0U ?
        strlen(template_text) : template_length;
    assembly.operand_plan_owner = target_qualified ?
        MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED :
        MINIC_ASM_OPERAND_PLAN_NONE;
    assembly.target_machine_form_id = target_qualified ? 1U : 0U;
    instruction.kind = target_qualified ?
        MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM :
        MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM;
    if (target_qualified) {
        instruction.value.structured_inline_asm.inline_asm_id = 0U;
        instruction.value.structured_inline_asm.operand_count = 1U;
    } else instruction.value.inline_asm_id = 0U;
    block.instructions = &instruction_id;
    block.instruction_count = 1U;
    functions[0].blocks = &block;
    functions[0].block_count = 1U;
    functions[0].instructions = &instruction;
    functions[0].instruction_count = 1U;
    functions[0].inline_asms = &assembly;
    functions[0].inline_asm_count = 1U;
    set.functions = functions;
    set.statuses = statuses;
    set.function_count = function_count;
    set.module.function_symbols = symbols;
    set.module.function_count = function_count;
    set.module.globals = globals;
    set.module.global_count = object_count;
    if (!minic_core_function_set_close_reachability(&set, false)) {
        fprintf(stderr, "CORE_INLINE_ASM_TOKEN=FAIL case=%s closure\n", label);
        return false;
    }
    for (i = 0U; i < function_count; ++i)
        if (symbols[i].is_referenced != function_live[i]) {
            fprintf(stderr, "CORE_INLINE_ASM_TOKEN=FAIL case=%s fn=%s\n",
                    label, function_names[i]);
            return false;
        }
    for (i = 0U; i < object_count; ++i)
        if (globals[i].emittable != object_live[i]) {
            fprintf(stderr, "CORE_INLINE_ASM_TOKEN=FAIL case=%s obj=%s\n",
                    label, object_names[i]);
            return false;
        }
    return true;
}

int main(void) {
    static const char *const functions[] = {
        "main", "hit_fn", "hit_fn", "dot.name", "prefix_hit_fn", "foo"
    };
    static const bool function_live[] = {true, true, true, true, false, true};
    static const char *const globals[] = {
        "hit_obj", "hit_obj", "dead_obj", "foo"
    };
    static const bool global_live[] = {true, true, false, true};
    static const char *const mnemonic_functions[] = {"main", "invtlb", "unused"};
    static const bool mnemonic_only_live[] = {true, false, false};
    static const bool mnemonic_reference_live[] = {true, true, false};
    static const char *const mnemonic_globals[] = {"invtlb", "unused"};
    static const bool mnemonic_global_live[] = {true, false};
    static const char *const unsafe_functions[] = {"main", "foo@VER", "miss"};
    static const bool unsafe_function_live[] = {true, true, false};
    static const char *const unsafe_globals[] = {"obj-name", "miss"};
    static const bool unsafe_global_live[] = {true, false};
    static const char *const nul_functions[] = {"main", "hit_fn", "unused"};
    static const bool nul_function_live[] = {true, true, false};
    static const char *const nul_globals[] = {"hit_obj", "unused"};
    static const bool nul_global_live[] = {true, false};
    static const char nul_asm[] = "call hit_fn\0.word hit_obj";

    if (!run_case("indexed-boundary-and-duplicates",
                  "call hit_fn@PLT; .quad hit_obj; mov dot.name, %foo; "
                  "# hit_fn (quoted hit_obj) and not prefix_hit_fn$1\n",
                  0U, false, functions, function_live, 6U,
                  globals, global_live, 4U) ||
        !run_case("target-mnemonic-only", "  invtlb %0, %1",
                  0U, true, mnemonic_functions, mnemonic_only_live, 3U,
                  mnemonic_globals, mnemonic_global_live, 2U) ||
        !run_case("target-mnemonic-referenced", " invtlb %0; .word invtlb",
                  0U, true, mnemonic_functions, mnemonic_reference_live, 3U,
                  mnemonic_globals, mnemonic_global_live, 2U) ||
        !run_case("unusual-symbol-fallback", "call foo@VER; .word obj-name",
                  0U, false, unsafe_functions, unsafe_function_live, 3U,
                  unsafe_globals, unsafe_global_live, 2U) ||
        !run_case("embedded-nul-raw-bytes", nul_asm, sizeof(nul_asm) - 1U,
                  false, nul_functions, nul_function_live, 3U,
                  nul_globals, nul_global_live, 2U)) return 1;
    puts("CORE_INLINE_ASM_TOKEN=PASS indexed_boundaries=6 duplicate_functions=2 duplicate_objects=2 comments=1 mnemonic_only=1 mnemonic_reference=1 unsafe_fallback=1 embedded_NUL=1");
    return 0;
}
