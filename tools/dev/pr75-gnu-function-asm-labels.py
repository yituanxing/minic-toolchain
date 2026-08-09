#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new, 1))


# Preserve the C identifier for semantic lookup and store an optional linker/assembler name.
replace_once(
    "src/frontend/ast.h",
    "typedef struct MinicFunction {\n    char *name;\n    size_t name_length;\n",
    "typedef struct MinicFunction {\n    char *name;\n    size_t name_length;\n    char *assembler_name;\n    size_t assembler_name_length;\n",
)
replace_once(
    "src/frontend/ast.h",
    "bool minic_c0_program_set_function_internal(MinicC0Program *program,\n                                            MinicFunctionId function_id,\n                                            bool is_internal);\n",
    "bool minic_c0_program_set_function_internal(MinicC0Program *program,\n                                            MinicFunctionId function_id,\n                                            bool is_internal);\n"
    "bool minic_c0_program_set_function_assembler_name(MinicC0Program *program,\n"
    "                                                  MinicFunctionId function_id,\n"
    "                                                  const char *name,\n"
    "                                                  size_t name_length);\n"
    "const char *minic_c0_function_symbol_name(const MinicFunction *function);\n",
)
replace_once(
    "src/frontend/ast.c",
    "    for (index = 0U; index < program->function_count; ++index) {\n        free(program->functions[index].name);\n    }\n",
    "    for (index = 0U; index < program->function_count; ++index) {\n        free(program->functions[index].name);\n        free(program->functions[index].assembler_name);\n    }\n",
)
replace_once(
    "src/frontend/ast_function.c",
    '#include "frontend/ast.h"\n',
    '#include "frontend/ast.h"\n\n#include <stdint.h>\n#include <stdlib.h>\n#include <string.h>\n',
)
ast_function = Path("src/frontend/ast_function.c")
text = ast_function.read_text()
text += r'''

bool minic_c0_program_set_function_assembler_name(MinicC0Program *program,
                                                  MinicFunctionId function_id,
                                                  const char *name,
                                                  size_t name_length) {
    MinicFunction *function;
    char *copy;

    if (program == NULL || function_id >= program->function_count || name == NULL ||
        name_length == 0U || name_length == SIZE_MAX) {
        return false;
    }
    function = &program->functions[function_id];
    if (function->assembler_name != NULL) {
        return function->assembler_name_length == name_length &&
               memcmp(function->assembler_name, name, name_length) == 0;
    }
    copy = (char *)malloc(name_length + 1U);
    if (copy == NULL) {
        return false;
    }
    (void)memcpy(copy, name, name_length);
    copy[name_length] = '\0';
    function->assembler_name = copy;
    function->assembler_name_length = name_length;
    return true;
}

const char *minic_c0_function_symbol_name(const MinicFunction *function) {
    if (function == NULL) {
        return NULL;
    }
    return function->assembler_name != NULL ? function->assembler_name : function->name;
}
'''
ast_function.write_text(text)

# Current real glibc uses adjacent plain string literals: __asm__("" "__xpg_strerror_r").
# Support that exact valid C form; reject escapes rather than silently decoding them wrongly.
parser = Path("src/frontend/parser_function.c")
text = parser.read_text()
anchor = "static bool function_signature_matches(const MinicFunction *function,\n"
helper = r'''static bool parse_gnu_function_asm_label(MinicParser *parser,
                                         char *buffer,
                                         size_t capacity,
                                         size_t *length,
                                         bool *has_label) {
    if (parser == NULL || buffer == NULL || length == NULL || has_label == NULL) {
        return false;
    }
    *length = 0U;
    *has_label = false;
    if (!function_identifier_is(parser, "__asm__") && !function_identifier_is(parser, "__asm")) {
        return true;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __asm__")) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        minic_parser_error(parser, "GNU function asm label requires a string literal");
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        size_t cursor;
        size_t end;

        if (parser->current.span.end.offset <= parser->current.span.begin.offset + 1U) {
            minic_parser_error(parser, "invalid GNU function asm label string");
            return false;
        }
        cursor = parser->current.span.begin.offset + 1U;
        end = parser->current.span.end.offset - 1U;
        while (cursor < end) {
            if (parser->source[cursor] == '\\') {
                minic_parser_error(parser, "escaped GNU function asm labels are not supported yet");
                return false;
            }
            if (*length + 1U >= capacity) {
                minic_parser_error(parser, "GNU function asm label is too long");
                return false;
            }
            buffer[*length] = parser->source[cursor];
            *length += 1U;
            cursor += 1U;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (*length == 0U) {
        minic_parser_error(parser, "GNU function asm label cannot be empty");
        return false;
    }
    buffer[*length] = '\0';
    *has_label = true;
    return minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after GNU asm label");
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"parser helper anchor: expected 1 match, found {text.count(anchor)}")
parser.write_text(text.replace(anchor, helper + anchor, 1))

replace_once(
    "src/frontend/parser_function.c",
    "    bool is_inline;\n    bool is_main;\n    bool is_variadic;\n",
    "    bool is_inline;\n    bool is_main;\n    bool is_variadic;\n"
    "    char assembler_name[256];\n    size_t assembler_name_length;\n    bool has_assembler_name;\n",
)
replace_once(
    "src/frontend/parser_function.c",
    "    is_inline = false;\n    is_variadic = false;\n    (void)memset(parameter_name_spans, 0, sizeof(parameter_name_spans));\n",
    "    is_inline = false;\n    is_variadic = false;\n    assembler_name_length = 0U;\n"
    "    has_assembler_name = false;\n    (void)memset(assembler_name, 0, sizeof(assembler_name));\n"
    "    (void)memset(parameter_name_spans, 0, sizeof(parameter_name_spans));\n",
)
replace_once(
    "src/frontend/parser_function.c",
    "    if (!parse_gnu_function_attributes(parser)) {\n        return false;\n    }\n",
    "    if (!parse_gnu_function_asm_label(parser, assembler_name, sizeof(assembler_name),\n"
    "                                      &assembler_name_length, &has_assembler_name) ||\n"
    "        !parse_gnu_function_attributes(parser)) {\n        return false;\n    }\n",
)
replace_once(
    "src/frontend/parser_function.c",
    "        return minic_parser_advance(parser);\n    }\n    if (!minic_type_is_integer(return_type) && !minic_type_is_void(return_type) &&\n",
    "        if (has_assembler_name &&\n"
    "            !minic_c0_program_set_function_assembler_name(\n"
    "                parser->program, function_id, assembler_name, assembler_name_length)) {\n"
    "            minic_parser_error(parser, \"conflicting or invalid GNU function asm label\");\n"
    "            return false;\n        }\n        return minic_parser_advance(parser);\n    }\n"
    "    if (!minic_type_is_integer(return_type) && !minic_type_is_void(return_type) &&\n",
)
replace_once(
    "src/frontend/parser_function.c",
    "    parser->current_function = function_id;\n    if (is_main) {\n",
    "    if (has_assembler_name &&\n"
    "        !minic_c0_program_set_function_assembler_name(\n"
    "            parser->program, function_id, assembler_name, assembler_name_length)) {\n"
    "        minic_parser_error(parser, \"conflicting or invalid GNU function asm label\");\n"
    "        return false;\n    }\n    parser->current_function = function_id;\n    if (is_main) {\n",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    '               fprintf(file, "  la a0, %s\\n", designator->name) >= 0;\n',
    '               fprintf(file, "  la a0, %s\\n", minic_c0_function_symbol_name(designator)) >= 0;\n',
)
replace_once(
    "src/target/riscv64/codegen_expression.c",
    '        } else if (fprintf(file, "  call %s\\n", direct_callee->name) < 0) {\n',
    '        } else if (fprintf(file, "  call %s\\n", minic_c0_function_symbol_name(direct_callee)) < 0) {\n',
)

codegen = Path("src/target/riscv64/codegen_function.c")
text = codegen.read_text()
reloc_old = 'fprintf(file, "  .dword %s\\n", function->name)'
if text.count(reloc_old) != 2:
    raise SystemExit(f"function relocation emission: expected 2 matches, found {text.count(reloc_old)}")
text = text.replace(reloc_old, 'fprintf(file, "  .dword %s\\n", minic_c0_function_symbol_name(function))')
old = "    size_t frame_size;\n    bool success;\n\n    if (function == NULL || !function->is_defined || function->name_length == 0U ||\n"
new = "    size_t frame_size;\n    bool success;\n    const char *symbol_name;\n\n    if (function == NULL || !function->is_defined || function->name_length == 0U ||\n"
if text.count(old) != 1:
    raise SystemExit("function symbol local anchor mismatch")
text = text.replace(old, new, 1)
old = "    frame_size = frame_layout.frame_size;\n\n    success = true;\n"
new = "    frame_size = frame_layout.frame_size;\n    symbol_name = minic_c0_function_symbol_name(function);\n    if (symbol_name == NULL || symbol_name[0] == '\\0') {\n        return false;\n    }\n\n    success = true;\n"
if text.count(old) != 1:
    raise SystemExit("function symbol initialization anchor mismatch")
text = text.replace(old, new, 1)
old = '        success = fprintf(file, ".globl %s\\n", function->name) >= 0;\n'
if text.count(old) != 1:
    raise SystemExit("function globl anchor mismatch")
text = text.replace(old, '        success = fprintf(file, ".globl %s\\n", symbol_name) >= 0;\n', 1)
pair_old = "                          function->name,\n                          function->name) >= 0;\n"
if text.count(pair_old) != 2:
    raise SystemExit(f"function definition symbol pair: expected 2 matches, found {text.count(pair_old)}")
text = text.replace(pair_old, "                          symbol_name,\n                          symbol_name) >= 0;\n")
codegen.write_text(text)

print("staged GNU function asm labels with effective linker-symbol propagation")
