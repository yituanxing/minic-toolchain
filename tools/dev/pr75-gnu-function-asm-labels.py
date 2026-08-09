#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new, 1))


# Keep the C identifier for semantic lookup, but record an optional GNU assembler/linker name.
replace_once(
    "src/frontend/ast.h",
    """typedef struct MinicFunction {\n    char *name;\n    size_t name_length;\n""",
    """typedef struct MinicFunction {\n    char *name;\n    size_t name_length;\n    char *assembler_name;\n    size_t assembler_name_length;\n""",
)
replace_once(
    "src/frontend/ast.h",
    """bool minic_c0_program_set_function_internal(MinicC0Program *program,\n                                            MinicFunctionId function_id,\n                                            bool is_internal);\n""",
    """bool minic_c0_program_set_function_internal(MinicC0Program *program,\n                                            MinicFunctionId function_id,\n                                            bool is_internal);\nbool minic_c0_program_set_function_assembler_name(MinicC0Program *program,\n                                                  MinicFunctionId function_id,\n                                                  const char *name,\n                                                  size_t name_length);\nconst char *minic_c0_function_symbol_name(const MinicFunction *function);\n""",
)
replace_once(
    "src/frontend/ast.c",
    """    for (index = 0U; index < program->function_count; ++index) {\n        free(program->functions[index].name);\n    }\n""",
    """    for (index = 0U; index < program->function_count; ++index) {\n        free(program->functions[index].name);\n        free(program->functions[index].assembler_name);\n    }\n""",
)

# Function metadata owns the assembler-name copy and rejects conflicting redeclarations.
replace_once(
    "src/frontend/ast_function.c",
    """#include \"frontend/ast.h\"\n""",
    """#include \"frontend/ast.h\"\n\n#include <stdlib.h>\n#include <string.h>\n""",
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

# Parse __asm__("...") after a GNU function declarator. Adjacent string literals are
# concatenated exactly as C requires. Decode the common C escapes used by assembler names;
# reject embedded NUL so a linker symbol can never be silently truncated.
parser = Path("src/frontend/parser_function.c")
text = parser.read_text()
anchor = "static bool function_signature_matches(const MinicFunction *function,\n"
helper = r'''static int gnu_asm_hex_digit(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static bool append_gnu_asm_string(MinicParser *parser,
                                  MinicSourceSpan span,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *length) {
    size_t cursor;
    size_t end;

    if (parser == NULL || buffer == NULL || length == NULL ||
        span.end.offset <= span.begin.offset + 1U) {
        return false;
    }
    cursor = span.begin.offset + 1U;
    end = span.end.offset - 1U;
    while (cursor < end) {
        unsigned int value;

        value = (unsigned int)(unsigned char)parser->source[cursor];
        cursor += 1U;
        if (value == (unsigned int)'\\') {
            char escape;

            if (cursor >= end) {
                minic_parser_error(parser, "unterminated escape in GNU assembler name");
                return false;
            }
            escape = parser->source[cursor];
            cursor += 1U;
            if (escape == 'x') {
                int digit;

                if (cursor >= end || gnu_asm_hex_digit(parser->source[cursor]) < 0) {
                    minic_parser_error(parser, "invalid hex escape in GNU assembler name");
                    return false;
                }
                value = 0U;
                while (cursor < end && (digit = gnu_asm_hex_digit(parser->source[cursor])) >= 0) {
                    if (value > (255U - (unsigned int)digit) / 16U) {
                        minic_parser_error(parser, "GNU assembler name escape exceeds one byte");
                        return false;
                    }
                    value = value * 16U + (unsigned int)digit;
                    cursor += 1U;
                }
            } else {
                switch (escape) {
                case '\\':
                case '\"':
                case '\'':
                case '?':
                    value = (unsigned int)(unsigned char)escape;
                    break;
                case 'a': value = (unsigned int)'\a'; break;
                case 'b': value = (unsigned int)'\b'; break;
                case 'f': value = (unsigned int)'\f'; break;
                case 'n': value = (unsigned int)'\n'; break;
                case 'r': value = (unsigned int)'\r'; break;
                case 't': value = (unsigned int)'\t'; break;
                case 'v': value = (unsigned int)'\v'; break;
                case '0': value = 0U; break;
                default:
                    minic_parser_error(parser, "unsupported escape in GNU assembler name");
                    return false;
                }
            }
        }
        if (value == 0U) {
            minic_parser_error(parser, "GNU assembler name cannot contain NUL");
            return false;
        }
        if (*length + 1U >= capacity) {
            minic_parser_error(parser, "GNU assembler name is too long");
            return false;
        }
        buffer[*length] = (char)value;
        *length += 1U;
    }
    return true;
}

static bool parse_gnu_function_asm_label(MinicParser *parser,
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
        MinicSourceSpan span;

        span = parser->current.span;
        if (!append_gnu_asm_string(parser, span, buffer, capacity, length) ||
            !minic_parser_advance(parser)) {
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
text = text.replace(anchor, helper + anchor, 1)
parser.write_text(text)

replace_once(
    "src/frontend/parser_function.c",
    """    bool is_inline;\n    bool is_main;\n    bool is_variadic;\n""",
    """    bool is_inline;\n    bool is_main;\n    bool is_variadic;\n    char assembler_name[256];\n    size_t assembler_name_length;\n    bool has_assembler_name;\n""",
)
replace_once(
    "src/frontend/parser_function.c",
    """    is_inline = false;\n    is_variadic = false;\n    (void)memset(parameter_name_spans, 0, sizeof(parameter_name_spans));\n""",
    """    is_inline = false;\n    is_variadic = false;\n    assembler_name_length = 0U;\n    has_assembler_name = false;\n    (void)memset(assembler_name, 0, sizeof(assembler_name));\n    (void)memset(parameter_name_spans, 0, sizeof(parameter_name_spans));\n""",
)
replace_once(
    "src/frontend/parser_function.c",
    """    if (!parse_gnu_function_attributes(parser)) {\n        return false;\n    }\n""",
    """    if (!parse_gnu_function_asm_label(parser,\n                                      assembler_name,\n                                      sizeof(assembler_name),\n                                      &assembler_name_length,\n                                      &has_assembler_name) ||\n        !parse_gnu_function_attributes(parser)) {\n        return false;\n    }\n""",
)
# Attach the label on declarations after the function object exists.
replace_once(
    "src/frontend/parser_function.c",
    """        return minic_parser_advance(parser);\n    }\n    if (!minic_type_is_integer(return_type) && !minic_type_is_void(return_type) &&\n""",
    """        if (has_assembler_name &&\n            !minic_c0_program_set_function_assembler_name(\n                parser->program, function_id, assembler_name, assembler_name_length)) {\n            minic_parser_error(parser, \"conflicting or invalid GNU function asm label\");\n            return false;\n        }\n        return minic_parser_advance(parser);\n    }\n    if (!minic_type_is_integer(return_type) && !minic_type_is_void(return_type) &&\n""",
)
# Definitions can carry the same GNU asm label; attach it after creation/reuse too.
replace_once(
    "src/frontend/parser_function.c",
    """    parser->current_function = function_id;\n    if (is_main) {\n""",
    """    if (has_assembler_name &&\n        !minic_c0_program_set_function_assembler_name(\n            parser->program, function_id, assembler_name, assembler_name_length)) {\n        minic_parser_error(parser, \"conflicting or invalid GNU function asm label\");\n        return false;\n    }\n    parser->current_function = function_id;\n    if (is_main) {\n""",
)

# Every emitted function reference must use the effective linker symbol.
replace_once(
    "src/target/riscv64/codegen_expression.c",
    """               fprintf(file, \"  la a0, %s\\n\", designator->name) >= 0;\n""",
    """               fprintf(file,\n                       \"  la a0, %s\\n\",\n                       minic_c0_function_symbol_name(designator)) >= 0;\n""",
)
replace_once(
    "src/target/riscv64/codegen_expression.c",
    """        } else if (fprintf(file, \"  call %s\\n\", direct_callee->name) < 0) {\n""",
    """        } else if (fprintf(file,\n                           \"  call %s\\n\",\n                           minic_c0_function_symbol_name(direct_callee)) < 0) {\n""",
)

# Static function-pointer relocations also target the assembler/linker name.
codegen_function = Path("src/target/riscv64/codegen_function.c")
text = codegen_function.read_text()
old = 'fprintf(file, "  .dword %s\\n", function->name)'
count = text.count(old)
if count != 2:
    raise SystemExit(f"function relocation emission: expected 2 matches, found {count}")
text = text.replace(old, 'fprintf(file, "  .dword %s\\n", minic_c0_function_symbol_name(function))')
codegen_function.write_text(text)

# A function definition's externally visible symbol follows the assembler name too;
# local return labels deliberately keep the C name because they are compiler-private.
replace_once(
    "src/target/riscv64/codegen_function.c",
    """    size_t frame_size;\n    bool success;\n\n    if (function == NULL || !function->is_defined || function->name_length == 0U ||\n""",
    """    size_t frame_size;\n    bool success;\n    const char *symbol_name;\n\n    if (function == NULL || !function->is_defined || function->name_length == 0U ||\n""",
)
replace_once(
    "src/target/riscv64/codegen_function.c",
    """    frame_size = frame_layout.frame_size;\n\n    success = true;\n""",
    """    frame_size = frame_layout.frame_size;\n    symbol_name = minic_c0_function_symbol_name(function);\n    if (symbol_name == NULL || symbol_name[0] == '\\0') {\n        return false;\n    }\n\n    success = true;\n""",
)
replace_once(
    "src/target/riscv64/codegen_function.c",
    """        success = fprintf(file, \".globl %s\\n\", function->name) >= 0;\n""",
    """        success = fprintf(file, \".globl %s\\n\", symbol_name) >= 0;\n""",
)
replace_once(
    "src/target/riscv64/codegen_function.c",
    """                          function->name,\n                          function->name) >= 0;\n""",
    """                          symbol_name,\n                          symbol_name) >= 0;\n""",
)
replace_once(
    "src/target/riscv64/codegen_function.c",
    """                          function->name,\n                          function->name) >= 0;\n    }\n    return success;\n}\n""",
    """                          symbol_name,\n                          symbol_name) >= 0;\n    }\n    return success;\n}\n""",
)

print("staged GNU function asm labels with effective linker-symbol propagation")
