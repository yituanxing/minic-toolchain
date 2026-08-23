#!/usr/bin/env python3
"""Lower direct function designators to first-class Core function-address values."""

from pathlib import Path

MARKER = "M81_FUNCTION_ADDRESS_VALUE"
IR = Path("src/core/core_ir.h")
IR_IMPL = Path("src/core/core_ir.c")
LOWER = Path("src/core/core_lower.c")
CODEGEN = Path("src/target/riscv64/core_codegen.c")


def replace_once(text: str, old: str, new: str, name: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M81 {name} anchor count={count}")
    return text.replace(old, new, 1)


def patch_ir() -> None:
    text = IR.read_text()
    if MARKER in text:
        print("M81 core_ir.h already applied")
        return

    text = replace_once(
        text,
        '''typedef uint32_t MinicCoreGlobalId;\ntypedef uint32_t MinicCoreCalleeId;\n''',
        '''typedef uint32_t MinicCoreGlobalId;\ntypedef uint32_t MinicCoreFunctionSymbolId;\ntypedef uint32_t MinicCoreCalleeId;\n''',
        "symbol-id",
    )
    text = replace_once(
        text,
        '''#define MINIC_CORE_GLOBAL_INVALID UINT32_MAX\n#define MINIC_CORE_CALLEE_INVALID UINT32_MAX\n''',
        '''#define MINIC_CORE_GLOBAL_INVALID UINT32_MAX\n#define MINIC_CORE_FUNCTION_SYMBOL_INVALID UINT32_MAX\n#define MINIC_CORE_CALLEE_INVALID UINT32_MAX\n''',
        "symbol-invalid",
    )
    text = replace_once(
        text,
        '''    MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS,\n    MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS,\n''',
        '''    MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS,\n    MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS,\n    /* M81_FUNCTION_ADDRESS_VALUE: first-class address of a function symbol. */\n    MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS,\n''',
        "instruction-kind",
    )
    text = replace_once(
        text,
        '''typedef struct MinicCoreCallee {\n''',
        '''typedef struct MinicCoreFunctionSymbol {\n    char *name;\n    size_t name_length;\n} MinicCoreFunctionSymbol;\n\ntypedef struct MinicCoreCallee {\n''',
        "symbol-struct",
    )
    text = replace_once(
        text,
        '''        MinicCoreObjectId object_id;\n        MinicCoreGlobalId global_id;\n        MinicCoreBlockId block_id;\n''',
        '''        MinicCoreObjectId object_id;\n        MinicCoreGlobalId global_id;\n        MinicCoreFunctionSymbolId function_symbol_id;\n        MinicCoreBlockId block_id;\n''',
        "symbol-payload",
    )
    text = replace_once(
        text,
        '''    MinicCoreGlobal *globals;\n    size_t global_count;\n    size_t global_capacity;\n    MinicCoreCallee *callees;\n''',
        '''    MinicCoreGlobal *globals;\n    size_t global_count;\n    size_t global_capacity;\n    MinicCoreFunctionSymbol *function_symbols;\n    size_t function_symbol_count;\n    size_t function_symbol_capacity;\n    MinicCoreCallee *callees;\n''',
        "symbol-storage",
    )
    text = replace_once(
        text,
        '''bool minic_core_function_add_block(MinicCoreFunction *function, MinicCoreBlockId *block_id);\n''',
        '''bool minic_core_function_add_block(MinicCoreFunction *function, MinicCoreBlockId *block_id);\nbool minic_core_function_add_function_symbol(MinicCoreFunction *function,\n                                             const char *name,\n                                             size_t name_length,\n                                             MinicCoreFunctionSymbolId *symbol_id);\n''',
        "symbol-api",
    )
    IR.write_text(text)
    print("M81 core_ir.h applied")


def patch_ir_impl() -> None:
    text = IR_IMPL.read_text()
    if MARKER in text:
        print("M81 core_ir.c already applied")
        return

    text = replace_once(
        text,
        '''    size_t global_index;\n    size_t inline_asm_index;\n''',
        '''    size_t global_index;\n    size_t function_symbol_index;\n    size_t inline_asm_index;\n''',
        "destroy-index",
    )
    text = replace_once(
        text,
        '''    for (global_index = 0U; global_index < function->global_count; ++global_index) {\n        free(function->globals[global_index].name);\n    }\n    for (inline_asm_index = 0U; inline_asm_index < function->inline_asm_count; ++inline_asm_index) {\n''',
        '''    for (global_index = 0U; global_index < function->global_count; ++global_index) {\n        free(function->globals[global_index].name);\n    }\n    for (function_symbol_index = 0U;\n         function_symbol_index < function->function_symbol_count;\n         ++function_symbol_index) {\n        free(function->function_symbols[function_symbol_index].name);\n    }\n    for (inline_asm_index = 0U; inline_asm_index < function->inline_asm_count; ++inline_asm_index) {\n''',
        "destroy-names",
    )
    text = replace_once(
        text,
        '''    free(function->globals);\n    free(function->callees);\n''',
        '''    free(function->globals);\n    free(function->function_symbols);\n    free(function->callees);\n''',
        "destroy-array",
    )

    add_anchor = '''static bool core_call_scalar_type(MinicType type) {\n'''
    add_symbol = '''/* M81_FUNCTION_ADDRESS_VALUE: function symbols are names, not call sites.\n   Keep them independent from MinicCoreCallee so merely taking a function\n   address never inherits scalar-call ABI restrictions. */\nbool minic_core_function_add_function_symbol(MinicCoreFunction *function,\n                                             const char *name,\n                                             size_t name_length,\n                                             MinicCoreFunctionSymbolId *symbol_id) {\n    char *name_copy;\n    size_t index;\n\n    if (function == NULL || name == NULL || name_length == 0U || symbol_id == NULL ||\n        function->function_symbol_count >= (size_t)UINT32_MAX) {\n        return false;\n    }\n    for (index = 0U; index < function->function_symbol_count; ++index) {\n        const MinicCoreFunctionSymbol *existing = &function->function_symbols[index];\n        if (existing->name_length == name_length &&\n            memcmp(existing->name, name, name_length) == 0) {\n            *symbol_id = (MinicCoreFunctionSymbolId)index;\n            return true;\n        }\n    }\n    name_copy = copy_name(name, name_length);\n    if (name_copy == NULL ||\n        !grow_array((void **)&function->function_symbols,\n                    &function->function_symbol_capacity,\n                    function->function_symbol_count,\n                    sizeof(*function->function_symbols))) {\n        free(name_copy);\n        return false;\n    }\n    function->function_symbols[function->function_symbol_count].name = name_copy;\n    function->function_symbols[function->function_symbol_count].name_length = name_length;\n    *symbol_id = (MinicCoreFunctionSymbolId)function->function_symbol_count;\n    function->function_symbol_count += 1U;\n    return true;\n}\n\nstatic bool core_call_scalar_type(MinicType type) {\n'''
    text = replace_once(text, add_anchor, add_symbol, "symbol-add")

    valid_anchor = '''    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS: {\n        MinicType pointer_type;\n\n        if (!instruction_result_is_valid(function, instruction) ||\n            instruction->value.global_id >= function->global_count ||\n            !minic_type_pointer_to(function->globals[instruction->value.global_id].type,\n                                   &pointer_type)) {\n            return false;\n        }\n        return minic_type_equal(pointer_type, instruction->type);\n    }\n'''
    valid = '''    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS: {\n        MinicType function_type;\n        MinicCoreFunctionSymbolId symbol_id;\n\n        symbol_id = instruction->value.function_symbol_id;\n        return instruction_result_is_valid(function, instruction) &&\n               symbol_id < function->function_symbol_count &&\n               function->function_symbols[symbol_id].name != NULL &&\n               function->function_symbols[symbol_id].name_length != 0U &&\n               minic_type_pointee(instruction->type, &function_type) &&\n               minic_type_is_function(function_type);\n    }\n'''
    text = replace_once(text, valid_anchor, valid_anchor + valid, "symbol-valid")

    dump_anchor = '''    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n        if (function == NULL || instruction->value.global_id >= function->global_count) {\n            return false;\n        }\n        return fprintf(output,\n                       "  %%%" PRIu32 " = global.addr @%s\\n",\n                       instruction->result,\n                       function->globals[instruction->value.global_id].name) >= 0;\n'''
    dump = '''    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:\n        if (function == NULL ||\n            instruction->value.function_symbol_id >= function->function_symbol_count) {\n            return false;\n        }\n        return fprintf(output,\n                       "  %%%" PRIu32 " = function.addr @%s\\n",\n                       instruction->result,\n                       function->function_symbols[instruction->value.function_symbol_id].name) >= 0;\n'''
    text = replace_once(text, dump_anchor, dump_anchor + dump, "symbol-dump")
    IR_IMPL.write_text(text)
    print("M81 core_ir.c applied")


def patch_lower() -> None:
    text = LOWER.read_text()
    if MARKER in text:
        print("M81 core_lower.c already applied")
        return

    anchor = '''    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {\n'''
    replacement = '''    /* M81_FUNCTION_ADDRESS_VALUE: a function designator is already a\n       pointer-to-function semantic value in the normalized AST. Core records\n       only the symbol identity; calling through that pointer is a later seam. */\n    if (expression->kind == MINIC_EXPRESSION_FUNCTION) {\n        const MinicFunction *designator;\n        const char *symbol_name;\n        size_t symbol_name_length;\n        MinicCoreFunctionSymbolId symbol_id;\n        MinicType function_type;\n\n        designator = minic_c0_program_function(\n            context->body->program, expression->value.function_id);\n        if (designator == NULL ||\n            !minic_type_pointee(expression->type, &function_type) ||\n            !minic_type_is_function(function_type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        symbol_name = designator->assembler_name != NULL ? designator->assembler_name\n                                                          : designator->name;\n        symbol_name_length = designator->assembler_name != NULL\n                                 ? designator->assembler_name_length\n                                 : designator->name_length;\n        if (symbol_name == NULL || symbol_name_length == 0U ||\n            !minic_core_function_add_function_symbol(\n                context->function, symbol_name, symbol_name_length, &symbol_id)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        (void)memset(&instruction, 0, sizeof(instruction));\n        instruction.kind = MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS;\n        instruction.span = expression->span;\n        instruction.type = expression->type;\n        instruction.result = MINIC_CORE_VALUE_INVALID;\n        instruction.value.function_symbol_id = symbol_id;\n        return minic_core_function_append_value_instruction(\n                   context->function, context->block_id, &instruction, value_id)\n                   ? MINIC_CORE_LOWER_OK\n                   : MINIC_CORE_LOWER_ERROR;\n    }\n    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {\n'''
    text = replace_once(text, anchor, replacement, "lower-function")
    LOWER.write_text(text)
    print("M81 core_lower.c applied")


def patch_codegen() -> None:
    text = CODEGEN.read_text()
    if MARKER in text:
        print("M81 core_codegen.c already applied")
        return

    support_anchor = '''    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n        return instruction->value.global_id < function->global_count &&\n               function->globals[instruction->value.global_id].name != NULL &&\n               function->globals[instruction->value.global_id].name_length != 0U;\n'''
    support = '''    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS: {\n        MinicType function_type;\n        MinicCoreFunctionSymbolId symbol_id = instruction->value.function_symbol_id;\n        return symbol_id < function->function_symbol_count &&\n               function->function_symbols[symbol_id].name != NULL &&\n               function->function_symbols[symbol_id].name_length != 0U &&\n               minic_type_pointee(instruction->type, &function_type) &&\n               minic_type_is_function(function_type);\n    }\n'''
    text = replace_once(text, support_anchor, support_anchor + support, "codegen-support")

    emit_anchor = '''    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n        if (instruction->value.global_id >= function->global_count ||\n            fprintf(file, "  la t0, %s\\n", function->globals[instruction->value.global_id].name) <\n                0) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, "t0");\n'''
    emit = '''    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:\n        if (instruction->value.function_symbol_id >= function->function_symbol_count ||\n            fprintf(file,\n                    "  la t0, %s\\n",\n                    function->function_symbols[instruction->value.function_symbol_id].name) < 0) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, "t0");\n'''
    text = replace_once(text, emit_anchor, emit_anchor + emit, "codegen-emit")
    CODEGEN.write_text(text)
    print("M81 core_codegen.c applied")


def main() -> int:
    patch_ir()
    patch_ir_impl()
    patch_lower()
    patch_codegen()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
