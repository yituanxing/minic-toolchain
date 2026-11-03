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
    function.core_return_type_id = void_type;
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
    function.inline_asms[assembly].is_goto = false;
    REQUIRE(!minic_core_function_verify(&function)); /* no non-goto borrowed label */
    function.inline_asms[assembly].is_goto = true;
    free(function.inline_asms[assembly].deferred_immediate_symbol);
    function.inline_asms[assembly].deferred_immediate_symbol = owned(
        "__minic_deferred_asm_immediate_17_0\n.globl injected");
    REQUIRE(function.inline_asms[assembly].deferred_immediate_symbol != NULL);
    REQUIRE(!minic_core_function_verify(&function));

    minic_core_function_destroy(&function);
    minic_core_type_context_destroy(&types);
    puts("CORE_SYMBOL_IDENTITY=PASS owned_local_symbols unique canonical deferred_goto injection_rejected");
    return 0;
}
