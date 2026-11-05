#include "core/core_ir.h"
#include "core/symbol_identity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr, "CORE_SYMBOL_IDENTITY=FAIL line=%d expression=%s\n", \
            __LINE__, #condition); return 1; \
} } while (0)

static char *owned(const char *text) {
    const size_t size = strlen(text) + 1U;
    char *copy = malloc(size);
    if (copy != NULL) memcpy(copy, text, size);
    return copy;
}

int main(void) {
    MinicCoreTypeContext types;
    MinicCoreType type = {0};
    MinicCoreFunction function;
    MinicCoreTypeId void_type;
    MinicCoreBlockId blocks[2];
    MinicCoreTerminator terminator = {0};
    MinicCoreInlineAsmId assembly;
    MinicCoreInlineAsmId typed_assembly;
    MinicCoreBlockId immediate_block;
    MinicCoreInstruction immediate_effect = {0};
    char *symbol = NULL;

    REQUIRE(minic_core_format_label_symbol(0U, &symbol));
    REQUIRE(strcmp(symbol, ".Luser_0") == 0);
    free(symbol);
    REQUIRE(minic_core_format_deferred_asm_symbol(17U, &symbol));
    REQUIRE(strcmp(symbol, "__minic_deferred_asm_immediate_17_0") == 0);
    free(symbol);
    REQUIRE(minic_core_label_symbol_valid(".Luser_71"));
    REQUIRE(!minic_core_label_symbol_valid(".Luser_01"));
    REQUIRE(!minic_core_label_symbol_valid(".Luser_9999999999999999999999999999999"));
    REQUIRE(!minic_core_label_symbol_valid(".Luser_7\n.globl takeover"));
    REQUIRE(!minic_core_deferred_asm_symbol_valid("__minic_deferred_asm_immediate_7_01"));

    minic_core_type_context_initialize(&types);
    type.kind = MINIC_CORE_TYPE_VOID;
    REQUIRE(minic_core_type_register(&types, &type, &void_type));
    minic_core_function_initialize(&function);
    function.name = owned("symbols");
    REQUIRE(function.name != NULL);
    function.name_length = strlen(function.name);
    REQUIRE(minic_core_function_define_signature(
        &function, &types, void_type, NULL, 0U,
        MINIC_CALLING_CONVENTION_DEFAULT, false));
    REQUIRE(minic_core_function_add_block(&function, &blocks[0]));
    REQUIRE(minic_core_function_add_block(&function, &blocks[1]));
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    terminator.return_object = MINIC_CORE_OBJECT_INVALID;
    REQUIRE(minic_core_function_set_terminator(&function, blocks[0], &terminator));
    REQUIRE(minic_core_function_set_terminator(&function, blocks[1], &terminator));
    function.blocks[0].local_label_symbol = owned(".Luser_0");
    function.blocks[1].local_label_symbol = owned(".Luser_17");
    REQUIRE(function.blocks[0].local_label_symbol != NULL &&
            function.blocks[1].local_label_symbol != NULL);
    REQUIRE(minic_core_function_attach_type_context(&function, &types));
    REQUIRE(minic_core_function_verify(&function));

    free(function.blocks[1].local_label_symbol);
    function.blocks[1].local_label_symbol = owned(".Luser_0");
    REQUIRE(function.blocks[1].local_label_symbol != NULL);
    REQUIRE(!minic_core_function_verify(&function)); /* duplicate local symbol */
    free(function.blocks[1].local_label_symbol);
    function.blocks[1].local_label_symbol = owned(".Luser_17\n.globl injected");
    REQUIRE(function.blocks[1].local_label_symbol != NULL);
    REQUIRE(!minic_core_function_verify(&function)); /* assembler injection */
    free(function.blocks[1].local_label_symbol);
    function.blocks[1].local_label_symbol = owned(".Luser_17");
    REQUIRE(function.blocks[1].local_label_symbol != NULL);
    REQUIRE(minic_core_function_verify(&function));

    REQUIRE(minic_core_function_add_opaque_inline_asm(
        &function, "nop %0", 6U, true, false, &assembly));
    function.inline_asms[assembly].is_goto = true;
    function.inline_asms[assembly].goto_target = blocks[1];
    function.inline_asms[assembly].deferred_immediate_symbol = owned(
        "__minic_deferred_asm_immediate_17_0");
    REQUIRE(function.inline_asms[assembly].deferred_immediate_symbol != NULL);
    REQUIRE(minic_core_function_verify(&function));
    /* A source-free producer cannot fabricate a dangling control-flow edge
     * merely by claiming an otherwise well-formed opaque ASM effect. */
    function.inline_asms[assembly].goto_target = MINIC_CORE_BLOCK_INVALID;
    REQUIRE(!minic_core_function_verify(&function));
    function.inline_asms[assembly].goto_target = blocks[1];
    function.inline_asms[assembly].is_goto = false;
    REQUIRE(!minic_core_function_verify(&function)); /* no non-goto borrowed label */
    function.inline_asms[assembly].is_goto = true;
    free(function.inline_asms[assembly].deferred_immediate_symbol);
    function.inline_asms[assembly].deferred_immediate_symbol = owned(
        "__minic_deferred_asm_immediate_17_0\n.globl injected");
    REQUIRE(function.inline_asms[assembly].deferred_immediate_symbol != NULL);
    REQUIRE(!minic_core_function_verify(&function));

    /* A Source-free producer cannot bypass typed Core immediate admission.
     * The instruction must be present: merely storing orphaned metadata does
     * not exercise the instruction verifier's execution contract. */
    free(function.inline_asms[assembly].deferred_immediate_symbol);
    function.inline_asms[assembly].deferred_immediate_symbol = NULL;
    REQUIRE(minic_core_function_add_block(&function, &immediate_block));
    REQUIRE(minic_core_function_add_opaque_inline_asm(
        &function, "nop", 3U, true, false, &typed_assembly));
    function.inline_asms[typed_assembly].is_goto = true;
    function.inline_asms[typed_assembly].goto_target = blocks[1];
    function.inline_asms[typed_assembly].resolved_immediate_count = 2U;
    function.inline_asms[typed_assembly].resolved_immediates[0].kind =
        MINIC_CORE_INLINE_ASM_IMMEDIATE_SYMBOL;
    function.inline_asms[typed_assembly].resolved_immediates[0].symbol = owned("key");
    function.inline_asms[typed_assembly].resolved_immediates[0].byte_addend = 4;
    function.inline_asms[typed_assembly].resolved_immediates[1].kind =
        MINIC_CORE_INLINE_ASM_IMMEDIATE_INTEGER;
    function.inline_asms[typed_assembly].resolved_immediates[1].integer = 3;
    REQUIRE(function.inline_asms[typed_assembly].resolved_immediates[0].symbol != NULL);
    immediate_effect.kind = MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM;
    immediate_effect.core_type_id = void_type;
    immediate_effect.value.inline_asm_id = typed_assembly;
    REQUIRE(minic_core_function_append_effect_instruction(
        &function, immediate_block, &immediate_effect));
    REQUIRE(minic_core_function_set_terminator(
        &function, immediate_block, &terminator));
    REQUIRE(minic_core_function_verify(&function));
    {
        MinicCoreInlineAsmImmediate *key =
            &function.inline_asms[typed_assembly].resolved_immediates[0];
        MinicCoreInlineAsmImmediate *flags =
            &function.inline_asms[typed_assembly].resolved_immediates[1];
        char *saved_key = key->symbol;
        key->symbol = (char *)"key+1";
        REQUIRE(!minic_core_function_verify(&function)); /* no assembler expression */
        key->symbol = saved_key;
        key->kind = MINIC_CORE_INLINE_ASM_IMMEDIATE_INTEGER;
        REQUIRE(!minic_core_function_verify(&function)); /* stale symbolic payload */
        key->kind = MINIC_CORE_INLINE_ASM_IMMEDIATE_SYMBOL;
        flags->byte_addend = 1;
        REQUIRE(!minic_core_function_verify(&function)); /* integer cannot relocate */
        flags->byte_addend = 0;
        flags->kind = MINIC_CORE_INLINE_ASM_IMMEDIATE_INVALID;
        REQUIRE(!minic_core_function_verify(&function)); /* missing typed slot */
        flags->kind = MINIC_CORE_INLINE_ASM_IMMEDIATE_INTEGER;
        function.inline_asms[typed_assembly].resolved_immediates[2].kind =
            MINIC_CORE_INLINE_ASM_IMMEDIATE_INTEGER;
        REQUIRE(!minic_core_function_verify(&function)); /* hidden trailing slot */
        function.inline_asms[typed_assembly].resolved_immediates[2].kind =
            MINIC_CORE_INLINE_ASM_IMMEDIATE_INVALID;
        REQUIRE(minic_core_function_verify(&function));
    }
    {
        MinicCoreInlineAsm *named = &function.inline_asms[typed_assembly];
        char *saved;
        /* The C-source GNU names must survive as owned Core facts. Neither
         * labels nor relocation symbols may masquerade as operand names. */
        free(named->template_text);
        named->template_text = owned(".word %[key]\n.word %[flags]\n.long %l[enabled]");
        REQUIRE(named->template_text != NULL);
        named->template_length = strlen(named->template_text);
        named->resolved_immediates[0].asm_name = owned("key");
        named->resolved_immediates[1].asm_name = owned("flags");
        named->goto_label_names[0] = owned("enabled");
        REQUIRE(named->resolved_immediates[0].asm_name != NULL &&
                named->resolved_immediates[1].asm_name != NULL &&
                named->goto_label_names[0] != NULL);
        REQUIRE(minic_core_function_verify(&function));

        saved = named->resolved_immediates[1].asm_name;
        named->resolved_immediates[1].asm_name = (char *)"key";
        REQUIRE(!minic_core_function_verify(&function)); /* duplicate binding */
        named->resolved_immediates[1].asm_name = saved;
        saved = named->resolved_immediates[0].asm_name;
        named->resolved_immediates[0].asm_name = (char *)"key+1";
        REQUIRE(!minic_core_function_verify(&function)); /* source injection */
        named->resolved_immediates[0].asm_name = saved;
        saved = named->goto_label_names[0];
        named->goto_label_names[0] = (char *)"key";
        REQUIRE(!minic_core_function_verify(&function)); /* label/input collision */
        named->goto_label_names[0] = (char *)"enabled; .globl forged";
        REQUIRE(!minic_core_function_verify(&function)); /* forged label syntax */
        named->goto_label_names[0] = saved;
        saved = named->template_text;
        named->template_text = (char *)".word %[missing]";
        named->template_length = strlen(named->template_text);
        REQUIRE(!minic_core_function_verify(&function)); /* unresolved name */
        named->template_text = (char *)".long %l[key]";
        named->template_length = strlen(named->template_text);
        REQUIRE(!minic_core_function_verify(&function)); /* wrong operand kind */
        named->template_text = (char *)".long %l[missing]";
        named->template_length = strlen(named->template_text);
        REQUIRE(!minic_core_function_verify(&function)); /* nonexistent C label */
        named->template_text = (char *)".word %%[not_an_operand]";
        named->template_length = strlen(named->template_text);
        REQUIRE(minic_core_function_verify(&function)); /* escaped GNU percent */
        named->template_text = saved;
        named->template_length = strlen(saved);
        named->goto_label_names[1] = (char *)"hidden";
        REQUIRE(!minic_core_function_verify(&function)); /* unused second label */
        named->goto_label_names[1] = NULL;
        named->resolved_immediates[2].asm_name = (char *)"hidden";
        REQUIRE(!minic_core_function_verify(&function)); /* hidden immediate slot */
        named->resolved_immediates[2].asm_name = NULL;
        named->resolved_immediates[3].kind =
            MINIC_CORE_INLINE_ASM_IMMEDIATE_INTEGER;
        REQUIRE(!minic_core_function_verify(&function)); /* fourth unused slot */
        named->resolved_immediates[3].kind =
            MINIC_CORE_INLINE_ASM_IMMEDIATE_INVALID;
        /* Typed immediate facts belong either to real asm-goto CFG or a
         * target-qualified STRUCTURED effect. An opaque textual ASM cannot
         * forge machine-source admission by setting metadata alone. */
        named->is_goto = false;
        named->operand_plan_owner = MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED;
        named->target_machine_form_id = 173U;
        REQUIRE(!minic_core_function_verify(&function));
        named->target_machine_form_id = 0U;
        named->operand_plan_owner = MINIC_ASM_OPERAND_PLAN_NONE;
        named->is_goto = true;
        REQUIRE(minic_core_function_verify(&function));
    }

    minic_core_function_destroy(&function);
    minic_core_type_context_destroy(&types);
    puts("CORE_SYMBOL_IDENTITY=PASS local_symbols deferred_goto typed_asm_integer_symbol named_operand_label_bindings forged_payload_rejected");
    return 0;
}
