#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def replace_region(text: str, start_marker: str, end_marker: str, replacement: str, label: str) -> str:
    start = text.find(start_marker)
    end = text.find(end_marker)
    if start < 0 or end < 0 or end <= start:
        raise SystemExit(f"{label}: region markers not found")
    return text[:start] + replacement + text[end:]


root = Path(__file__).resolve().parents[2]

# Shared DataLayout v0. The active default is RV64, but all layout queries are
# parameterized so the parser no longer owns a private RV64 layout algorithm.
path = root / "src/target/data_layout.h"
path.parent.mkdir(parents=True, exist_ok=True)
path.write_text('''#ifndef MINIC_TARGET_DATA_LAYOUT_H\n#define MINIC_TARGET_DATA_LAYOUT_H\n\n#include "frontend/ast.h"\n\n#include <stdbool.h>\n#include <stddef.h>\n\ntypedef struct MinicDataLayout {\n    size_t pointer_size;\n    size_t pointer_alignment;\n    size_t integer_size[MINIC_INTEGER_RANK_INT128 + 1U];\n    size_t integer_alignment[MINIC_INTEGER_RANK_INT128 + 1U];\n    size_t float_size;\n    size_t float_alignment;\n    size_t double_size;\n    size_t double_alignment;\n} MinicDataLayout;\n\nconst MinicDataLayout *minic_default_data_layout(void);\nbool minic_data_layout_type(const MinicDataLayout *layout,\n                            const MinicC0Program *program,\n                            MinicType type,\n                            size_t *size,\n                            size_t *alignment);\nbool minic_data_layout_record_field_offset(const MinicDataLayout *layout,\n                                           const MinicC0Program *program,\n                                           const MinicRecord *record,\n                                           size_t field_index,\n                                           size_t *offset);\n\n#endif\n''')

path = root / "src/target/data_layout.c"
path.write_text('''#include "target/data_layout.h"\n\n#include <stdint.h>\n\n#define MINIC_DATA_LAYOUT_MAX_DEPTH 64U\n\nstatic const MinicDataLayout minic_rv64_data_layout = {\n    .pointer_size = 8U,\n    .pointer_alignment = 8U,\n    .integer_size = {0U, 1U, 1U, 2U, 4U, 8U, 8U, 16U},\n    .integer_alignment = {0U, 1U, 1U, 2U, 4U, 8U, 8U, 16U},\n    .float_size = 4U,\n    .float_alignment = 4U,\n    .double_size = 8U,\n    .double_alignment = 8U,\n};\n\nconst MinicDataLayout *minic_default_data_layout(void) {\n    return &minic_rv64_data_layout;\n}\n\nstatic bool minic_data_layout_align_up(size_t value, size_t alignment, size_t *result) {\n    size_t remainder;\n    size_t padding;\n\n    if (result == NULL || alignment == 0U) {\n        return false;\n    }\n    remainder = value % alignment;\n    padding = remainder == 0U ? 0U : alignment - remainder;\n    if (value > SIZE_MAX - padding) {\n        return false;\n    }\n    *result = value + padding;\n    return true;\n}\n\nstatic bool minic_data_layout_type_depth(const MinicDataLayout *layout,\n                                         const MinicC0Program *program,\n                                         MinicType type,\n                                         unsigned int depth,\n                                         size_t *size,\n                                         size_t *alignment);\n\nstatic bool minic_data_layout_record_depth(const MinicDataLayout *layout,\n                                           const MinicC0Program *program,\n                                           const MinicRecord *record,\n                                           unsigned int depth,\n                                           size_t requested_field,\n                                           size_t *requested_offset,\n                                           size_t *size,\n                                           size_t *alignment) {\n    size_t storage_size;\n    size_t record_alignment;\n    size_t index;\n\n    if (layout == NULL || program == NULL || record == NULL || size == NULL || alignment == NULL ||\n        !record->is_complete || depth > MINIC_DATA_LAYOUT_MAX_DEPTH) {\n        return false;\n    }\n    storage_size = 0U;\n    record_alignment = 1U;\n    for (index = 0U; index < record->field_count; ++index) {\n        const MinicRecordField *field;\n        size_t element_size;\n        size_t field_size;\n        size_t field_alignment;\n        size_t field_offset;\n\n        field = &record->fields[index];\n        if (field->element_count == 0U ||\n            !minic_data_layout_type_depth(\n                layout, program, field->type, depth + 1U, &element_size, &field_alignment) ||\n            element_size > SIZE_MAX / field->element_count) {\n            return false;\n        }\n        if (field->is_bit_field) {\n            size_t storage_bits;\n\n            if (!minic_type_is_integer(field->type) || field->element_count != 1U ||\n                field->is_flexible_array || element_size > SIZE_MAX / 8U) {\n                return false;\n            }\n            storage_bits = element_size * 8U;\n            if (field->bit_width > storage_bits ||\n                (field->bit_width != 0U && field->bit_width != storage_bits)) {\n                return false;\n            }\n            field_size = field->bit_width == 0U ? 0U : element_size;\n            if (record->is_union) {\n                field_offset = 0U;\n                if (field_size > storage_size) {\n                    storage_size = field_size;\n                }\n            } else if (record->is_packed) {\n                field_offset = storage_size;\n                if (field_size != 0U) {\n                    if (field_offset > SIZE_MAX - field_size) {\n                        return false;\n                    }\n                    storage_size = field_offset + field_size;\n                }\n            } else {\n                if (!minic_data_layout_align_up(storage_size, field_alignment, &field_offset)) {\n                    return false;\n                }\n                if (field_size != 0U) {\n                    if (field_offset > SIZE_MAX - field_size) {\n                        return false;\n                    }\n                    storage_size = field_offset + field_size;\n                } else {\n                    storage_size = field_offset;\n                }\n            }\n            if (!record->is_packed && field_alignment > record_alignment) {\n                record_alignment = field_alignment;\n            }\n        } else {\n            field_size = (field->is_flexible_array || field->is_zero_length_array)\n                             ? 0U\n                             : element_size * field->element_count;\n            if (field->explicit_alignment != 0U) {\n                if ((field->explicit_alignment & (field->explicit_alignment - 1U)) != 0U) {\n                    return false;\n                }\n                if (field->explicit_alignment > field_alignment) {\n                    field_alignment = field->explicit_alignment;\n                }\n            }\n            if (record->is_union) {\n                field_offset = 0U;\n                if (field_size > storage_size) {\n                    storage_size = field_size;\n                }\n            } else if (record->is_packed && field->explicit_alignment == 0U) {\n                field_offset = storage_size;\n                if (field_offset > SIZE_MAX - field_size) {\n                    return false;\n                }\n                storage_size = field_offset + field_size;\n            } else {\n                if (!minic_data_layout_align_up(storage_size, field_alignment, &field_offset) ||\n                    field_offset > SIZE_MAX - field_size) {\n                    return false;\n                }\n                storage_size = field_offset + field_size;\n            }\n            if ((!record->is_packed || field->explicit_alignment != 0U) &&\n                field_alignment > record_alignment) {\n                record_alignment = field_alignment;\n            }\n        }\n        if (requested_offset != NULL && index == requested_field) {\n            *requested_offset = field_offset;\n        }\n    }\n    if (record->explicit_alignment != 0U) {\n        if ((record->explicit_alignment & (record->explicit_alignment - 1U)) != 0U) {\n            return false;\n        }\n        if (record->explicit_alignment > record_alignment) {\n            record_alignment = record->explicit_alignment;\n        }\n    }\n    if (!minic_data_layout_align_up(storage_size, record_alignment, size)) {\n        return false;\n    }\n    *alignment = record_alignment;\n    return true;\n}\n\nstatic bool minic_data_layout_type_depth(const MinicDataLayout *layout,\n                                         const MinicC0Program *program,\n                                         MinicType type,\n                                         unsigned int depth,\n                                         size_t *size,\n                                         size_t *alignment) {\n    if (layout == NULL || program == NULL || size == NULL || alignment == NULL ||\n        depth > MINIC_DATA_LAYOUT_MAX_DEPTH) {\n        return false;\n    }\n    if (minic_type_is_pointer(type)) {\n        *size = layout->pointer_size;\n        *alignment = layout->pointer_alignment;\n        return true;\n    }\n    if (minic_type_is_integer(type)) {\n        size_t rank = (size_t)type.integer_rank;\n\n        if (rank == (size_t)MINIC_INTEGER_RANK_NONE ||\n            rank > (size_t)MINIC_INTEGER_RANK_INT128 || layout->integer_size[rank] == 0U ||\n            layout->integer_alignment[rank] == 0U) {\n            return false;\n        }\n        *size = layout->integer_size[rank];\n        *alignment = layout->integer_alignment[rank];\n        return true;\n    }\n    if (minic_type_is_float(type)) {\n        *size = layout->float_size;\n        *alignment = layout->float_alignment;\n        return true;\n    }\n    if (minic_type_is_double(type)) {\n        *size = layout->double_size;\n        *alignment = layout->double_alignment;\n        return true;\n    }\n    if (minic_type_is_array(type)) {\n        const MinicArrayType *array_type;\n        size_t element_size;\n        size_t element_alignment;\n\n        array_type = minic_c0_program_array_type(program, type.array_type_id);\n        if (array_type == NULL || array_type->element_count == 0U ||\n            !minic_data_layout_type_depth(layout,\n                                          program,\n                                          array_type->element_type,\n                                          depth + 1U,\n                                          &element_size,\n                                          &element_alignment) ||\n            element_size > SIZE_MAX / array_type->element_count) {\n            return false;\n        }\n        *size = element_size * array_type->element_count;\n        *alignment = element_alignment;\n        return true;\n    }\n    if (minic_type_is_record(type)) {\n        const MinicRecord *record;\n\n        record = minic_c0_program_record(program, type.record_id);\n        return minic_data_layout_record_depth(\n            layout, program, record, depth + 1U, SIZE_MAX, NULL, size, alignment);\n    }\n    return false;\n}\n\nbool minic_data_layout_type(const MinicDataLayout *layout,\n                            const MinicC0Program *program,\n                            MinicType type,\n                            size_t *size,\n                            size_t *alignment) {\n    return minic_data_layout_type_depth(layout, program, type, 0U, size, alignment);\n}\n\nbool minic_data_layout_record_field_offset(const MinicDataLayout *layout,\n                                           const MinicC0Program *program,\n                                           const MinicRecord *record,\n                                           size_t field_index,\n                                           size_t *offset) {\n    size_t size;\n    size_t alignment;\n\n    if (record == NULL || offset == NULL || field_index >= record->field_count) {\n        return false;\n    }\n    return minic_data_layout_record_depth(\n        layout, program, record, 0U, field_index, offset, &size, &alignment);\n}\n''')

# Parser carries an explicit DataLayout dependency. The public parser API still
# defaults to RV64 for now; target selection will be a later TargetInfo step.
path = root / "src/frontend/parser_internal.h"
text = path.read_text()
text = replace_once(
    text,
    '''#include "minic/compiler.h"\n''',
    '''#include "minic/compiler.h"\n#include "target/data_layout.h"\n''',
    "parser-data-layout-include",
)
text = replace_once(
    text,
    '''    MinicDiagnostic *diagnostic;\n    MinicC0Program *program;\n''',
    '''    MinicDiagnostic *diagnostic;\n    MinicC0Program *program;\n    const MinicDataLayout *data_layout;\n''',
    "parser-data-layout-field",
)
text = replace_once(
    text,
    '''bool minic_parser_parse_integer_constant_expression(MinicParser *parser, int64_t *value);\n''',
    '''bool minic_parser_parse_integer_constant_expression(MinicParser *parser, int64_t *value);\nbool minic_parser_parse_alignof_type_value(MinicParser *parser,\n                                           int64_t *value,\n                                           MinicSourceSpan *span);\n''',
    "parser-alignof-contract",
)
path.write_text(text)

path = root / "src/frontend/parser_function.c"
text = path.read_text()
text = replace_once(
    text,
    '''    parser.diagnostic = diagnostic;\n    parser.program = program;\n    parser.current_block = MINIC_BLOCK_INVALID;\n''',
    '''    parser.diagnostic = diagnostic;\n    parser.program = program;\n    parser.data_layout = minic_default_data_layout();\n    parser.current_block = MINIC_BLOCK_INVALID;\n''',
    "parser-default-data-layout",
)
path.write_text(text)

# Shared alignof type-query parser used by ordinary expressions and token-based ICE.
path = root / "src/frontend/parser_type_query.c"
path.write_text('''#include "frontend/parser_internal.h"\n\n#include <stdint.h>\n\nbool minic_parser_parse_alignof_type_value(MinicParser *parser,\n                                           int64_t *value,\n                                           MinicSourceSpan *span) {\n    MinicSourcePosition begin;\n    MinicSourcePosition end;\n    MinicType measured_type;\n    size_t measured_size;\n    size_t measured_alignment;\n\n    if (parser == NULL || value == NULL || parser->current.kind != MINIC_TOKEN_KW_ALIGNOF) {\n        if (parser != NULL) {\n            minic_parser_error(parser, "expected alignof type query");\n        }\n        return false;\n    }\n    begin = parser->current.span.begin;\n    if (!minic_parser_advance(parser) ||\n        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after alignof") ||\n        !minic_parser_parse_type_name(parser, &measured_type)) {\n        return false;\n    }\n    if (parser->current.kind != MINIC_TOKEN_RPAREN) {\n        minic_parser_error(parser, "expected ')' after alignof type");\n        return false;\n    }\n    end = parser->current.span.end;\n    if (!minic_data_layout_type(parser->data_layout,\n                                parser->program,\n                                measured_type,\n                                &measured_size,\n                                &measured_alignment) ||\n        measured_alignment > (size_t)INT64_MAX) {\n        minic_parser_error(parser, "alignof requires a complete object type");\n        return false;\n    }\n    (void)measured_size;\n    *value = (int64_t)measured_alignment;\n    if (span != NULL) {\n        span->begin = begin;\n        span->end = end;\n    }\n    return minic_parser_advance(parser);\n}\n''')

# Remove the parser-owned RV64 record-layout algorithm and route token-based
# sizeof/offsetof through DataLayout.
path = root / "src/frontend/parser_core.c"
text = path.read_text()
text = replace_once(
    text,
    '''static bool constant_type_layout(const MinicC0Program *program,\n                                 MinicType type,\n                                 unsigned int depth,\n                                 uint64_t *size,\n                                 uint64_t *alignment);\n\nstatic bool array_bound_type_size(const MinicC0Program *program, MinicType type, uint64_t *size) {\n    uint64_t alignment;\n\n    return constant_type_layout(program, type, 0U, size, &alignment);\n}\n''',
    '''static bool array_bound_type_size(const MinicParser *parser, MinicType type, uint64_t *size) {\n    size_t measured_size;\n    size_t measured_alignment;\n\n    if (parser == NULL || size == NULL ||\n        !minic_data_layout_type(\n            parser->data_layout, parser->program, type, &measured_size, &measured_alignment)) {\n        return false;\n    }\n    (void)measured_alignment;\n    *size = (uint64_t)measured_size;\n    return true;\n}\n''',
    "parser-data-layout-size-query",
)
text = replace_once(
    text,
    '''            !array_bound_type_size(parser->program, measured_type, &measured_size)) {\n''',
    '''            !array_bound_type_size(parser, measured_type, &measured_size)) {\n''',
    "parser-sizeof-data-layout-call",
)
replacement = '''static bool constant_record_member_offset(const MinicParser *parser,\n                                          const MinicRecord *record,\n                                          const char *name,\n                                          size_t name_length,\n                                          uint64_t *offset) {\n    size_t field_index;\n    size_t field_offset;\n\n    if (parser == NULL || record == NULL || name == NULL || offset == NULL ||\n        !record->is_complete) {\n        return false;\n    }\n    for (field_index = 0U; field_index < record->field_count; ++field_index) {\n        const MinicRecordField *field = &record->fields[field_index];\n\n        if (field->name_length == name_length && memcmp(field->name, name, name_length) == 0) {\n            if (field->is_bit_field ||\n                !minic_data_layout_record_field_offset(parser->data_layout,\n                                                       parser->program,\n                                                       record,\n                                                       field_index,\n                                                       &field_offset)) {\n                return false;\n            }\n            *offset = (uint64_t)field_offset;\n            return true;\n        }\n    }\n    return false;\n}\n\n'''
text = replace_region(
    text,
    "static bool constant_align_up(",
    "static bool current_is_builtin_offsetof_constant(",
    replacement,
    "remove-parser-private-data-layout",
)
text = replace_once(
    text,
    '''        !constant_record_member_offset(parser->program,\n                                       record,\n''',
    '''        !constant_record_member_offset(parser,\n                                       record,\n''',
    "parser-offsetof-data-layout-call",
)
text = replace_once(
    text,
    '''    if (current_is_builtin_offsetof_constant(parser)) {\n        return parse_offsetof_integer_constant(parser, value);\n    }\n''',
    '''    if (current_is_builtin_offsetof_constant(parser)) {\n        return parse_offsetof_integer_constant(parser, value);\n    }\n    if (parser->current.kind == MINIC_TOKEN_KW_ALIGNOF) {\n        return minic_parser_parse_alignof_type_value(parser, value, NULL);\n    }\n''',
    "shared-ice-alignof",
)
path.write_text(text)

# Add alignof as a normal expression, lowered to an integer constant AST node.
path = root / "src/frontend/parser_expression.c"
text = path.read_text()
anchor = '''static bool current_is_sizeof(const MinicParser *parser) {\n    return parser->current.kind == MINIC_TOKEN_KW_SIZEOF;\n}\n\n'''
addition = '''static bool current_is_alignof(const MinicParser *parser) {\n    return parser->current.kind == MINIC_TOKEN_KW_ALIGNOF;\n}\n\nstatic bool parse_alignof(MinicParser *parser, MinicExpressionId *expression_id) {\n    MinicExpression expression;\n    MinicSourceSpan span;\n    int64_t alignment;\n\n    if (!minic_parser_parse_alignof_type_value(parser, &alignment, &span)) {\n        return false;\n    }\n    (void)memset(&expression, 0, sizeof(expression));\n    expression.kind = MINIC_EXPRESSION_INTEGER;\n    expression.span = span;\n    expression.type = minic_type_unsigned_long();\n    expression.value_category = MINIC_VALUE_RVALUE;\n    expression.value.integer_value = alignment;\n    return minic_parser_add_expression(parser, &expression, expression_id);\n}\n\n'''
text = replace_once(text, anchor, anchor + addition, "expression-alignof-parser")
text = replace_once(
    text,
    '''    if (current_is_sizeof(parser)) {\n        return parse_sizeof(parser, expression_id);\n    }\n''',
    '''    if (current_is_sizeof(parser)) {\n        return parse_sizeof(parser, expression_id);\n    }\n    if (current_is_alignof(parser)) {\n        return parse_alignof(parser, expression_id);\n    }\n''',
    "expression-alignof-dispatch",
)
path.write_text(text)

# Token/lexer spellings: C11 _Alignof and GNU __alignof / __alignof__ share one semantic token.
path = root / "src/frontend/token.h"
text = path.read_text()
text = replace_once(
    text,
    '''    MINIC_TOKEN_KW_STATIC,\n    MINIC_TOKEN_KW_SIZEOF,\n    MINIC_TOKEN_KW_RETURN,\n''',
    '''    MINIC_TOKEN_KW_STATIC,\n    MINIC_TOKEN_KW_SIZEOF,\n    MINIC_TOKEN_KW_ALIGNOF,\n    MINIC_TOKEN_KW_RETURN,\n''',
    "token-alignof",
)
path.write_text(text)

path = root / "src/frontend/token.c"
text = path.read_text()
text = replace_once(
    text,
    '''    case MINIC_TOKEN_KW_SIZEOF:\n        return "sizeof";\n    case MINIC_TOKEN_KW_RETURN:\n''',
    '''    case MINIC_TOKEN_KW_SIZEOF:\n        return "sizeof";\n    case MINIC_TOKEN_KW_ALIGNOF:\n        return "alignof";\n    case MINIC_TOKEN_KW_RETURN:\n''',
    "token-name-alignof",
)
path.write_text(text)

path = root / "src/frontend/lexer.c"
text = path.read_text()
text = replace_once(
    text,
    '''    if (length == 6U && memcmp(text, "sizeof", 6U) == 0) {\n        return MINIC_TOKEN_KW_SIZEOF;\n    }\n    if (length == 6U && memcmp(text, "return", 6U) == 0) {\n''',
    '''    if (length == 6U && memcmp(text, "sizeof", 6U) == 0) {\n        return MINIC_TOKEN_KW_SIZEOF;\n    }\n    if ((length == 8U && memcmp(text, "_Alignof", 8U) == 0) ||\n        (length == 9U && memcmp(text, "__alignof", 9U) == 0) ||\n        (length == 11U && memcmp(text, "__alignof__", 11U) == 0)) {\n        return MINIC_TOKEN_KW_ALIGNOF;\n    }\n    if (length == 6U && memcmp(text, "return", 6U) == 0) {\n''',
    "lexer-alignof-keywords",
)
path.write_text(text)

# RV64 target type queries now delegate to shared DataLayout v0.
path = root / "src/target/riscv64/layout.c"
text = path.read_text()
text = replace_once(
    text,
    '''#include "target/riscv64/layout.h"\n''',
    '''#include "target/riscv64/layout.h"\n\n#include "target/data_layout.h"\n''',
    "rv64-layout-data-layout-include",
)
text = replace_region(
    text,
    "bool minic_riscv64_type_layout(",
    "static bool minic_riscv64_align_up(",
    '''bool minic_riscv64_type_layout(const MinicC0Program *program,\n                               MinicType type,\n                               size_t *size,\n                               size_t *alignment) {\n    return minic_data_layout_type(minic_default_data_layout(), program, type, size, alignment);\n}\n\n''',
    "rv64-type-layout-wrapper",
)
path.write_text(text)

# Build graph includes the shared DataLayout and alignof parser seam.
path = root / "Makefile"
text = path.read_text()
text = replace_once(
    text,
    '''\tsrc/frontend/parser_statement.c \\\n\tsrc/frontend/parser_string.c \\\n\tsrc/frontend/parser_type.c \\\n''',
    '''\tsrc/frontend/parser_statement.c \\\n\tsrc/frontend/parser_string.c \\\n\tsrc/frontend/parser_type.c \\\n\tsrc/frontend/parser_type_query.c \\\n''',
    "makefile-parser-type-query",
)
text = replace_once(
    text,
    '''\tsrc/frontend/type.c \\\n\tsrc/target/riscv64/layout.c \\\n''',
    '''\tsrc/frontend/type.c \\\n\tsrc/target/data_layout.c \\\n\tsrc/target/riscv64/layout.c \\\n''',
    "makefile-data-layout",
)
text = replace_once(
    text,
    '''LAYOUT_TEST_SOURCES := \\\n\tsrc/frontend/ast.c \\\n\tsrc/frontend/type.c \\\n\tsrc/target/riscv64/layout.c \\\n''',
    '''LAYOUT_TEST_SOURCES := \\\n\tsrc/frontend/ast.c \\\n\tsrc/frontend/type.c \\\n\tsrc/target/data_layout.c \\\n\tsrc/target/riscv64/layout.c \\\n''',
    "layout-test-data-layout",
)
path.write_text(text)

# Token and lexer regression coverage.
path = root / "tests/frontend/token_model_test.c"
text = path.read_text()
text = replace_once(
    text,
    '''        expect_name(MINIC_TOKEN_KW_SIZEOF, "sizeof") != 0 ||\n''',
    '''        expect_name(MINIC_TOKEN_KW_SIZEOF, "sizeof") != 0 ||\n        expect_name(MINIC_TOKEN_KW_ALIGNOF, "alignof") != 0 ||\n''',
    "token-model-alignof",
)
path.write_text(text)

path = root / "tests/frontend/lexer_test.c"
text = path.read_text()
anchor = '''static int test_static_assert_keyword_boundaries(void)\n{\n'''
addition = '''static int test_alignof_keyword_boundaries(void)\n{\n    static const char source[] = "_Alignof __alignof__ __alignof alignof";\n    MinicLexer lexer;\n\n    minic_lexer_initialize(&lexer, "alignof.c", source, sizeof(source) - 1U);\n    if (expect_token(&lexer, MINIC_TOKEN_KW_ALIGNOF, 1U, 1U) != 0 ||\n        expect_token(&lexer, MINIC_TOKEN_KW_ALIGNOF, 1U, 10U) != 0 ||\n        expect_token(&lexer, MINIC_TOKEN_KW_ALIGNOF, 1U, 22U) != 0 ||\n        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 32U) != 0 ||\n        expect_token(&lexer, MINIC_TOKEN_EOF, 1U, 39U) != 0) {\n        return 1;\n    }\n    return 0;\n}\n\n'''
text = replace_once(text, anchor, addition + anchor, "lexer-alignof-test")
text = replace_once(
    text,
    '''        test_comparison_operators() != 0 ||\n        test_static_assert_keyword_boundaries() != 0 ||\n''',
    '''        test_comparison_operators() != 0 ||\n        test_alignof_keyword_boundaries() != 0 ||\n        test_static_assert_keyword_boundaries() != 0 ||\n''',
    "lexer-alignof-main",
)
path.write_text(text)

# Focused semantic gate: both GNU and C11 spellings, scalar and aligned record,
# runtime expression plus static-assert constant evaluation.
path = root / "tests/compiler/c0/alignof_type_query.c"
path.write_text('''struct OverAligned {\n    char byte;\n} __attribute__((__aligned__(16)));\n\n_Static_assert(__alignof__(unsigned long) == 8, "gnu ulong alignment");\n_Static_assert(__alignof(unsigned long long) == 8, "gnu alias alignment");\n_Static_assert(_Alignof(struct OverAligned) == 16, "c11 record alignment");\n\nunsigned long alignof_ulong(void) {\n    return __alignof__(unsigned long);\n}\n\nunsigned long alignof_record(void) {\n    return _Alignof(struct OverAligned);\n}\n\nint main(void) {\n    return alignof_ulong() == 8 && alignof_record() == 16 ? 0 : 1;\n}\n''')

path = root / "tests/compiler/c0/run-alignof-type-query.sh"
path.write_text('''#!/bin/sh\nset -eu\n\nroot=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)\nminic=${MINIC:-"$root/build/debug/bin/minic"}\nhost_cc=${HOST_CC:-${CC:-cc}}\nwork=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-alignof-type-query\n\nrm -rf "$work"\nmkdir -p "$work"\n\n"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/alignof_type_query.c" \\\n    -o "$work/alignof_type_query.i"\n"$minic" -S "$work/alignof_type_query.i" -o "$work/alignof_type_query.s"\ntest -s "$work/alignof_type_query.s"\ngrep -F 'alignof_ulong:' "$work/alignof_type_query.s" >/dev/null\ngrep -F 'alignof_record:' "$work/alignof_type_query.s" >/dev/null\ngrep -F '  li a0, 8' "$work/alignof_type_query.s" >/dev/null\ngrep -F '  li a0, 16' "$work/alignof_type_query.s" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/alignof_type_query spellings=_Alignof,__alignof,__alignof__ scalar=8 aligned-record=16 service=shared-data-layout static-assert=1'\n''')

path = root / "tools/dev/pr76-focused.sh"
text = path.read_text()
text = replace_once(
    text,
    '''sh tests/compiler/c0/run-builtin-offsetof.sh\n''',
    '''sh tests/compiler/c0/run-builtin-offsetof.sh\nsh tests/compiler/c0/run-alignof-type-query.sh\n''',
    "focused-alignof-gate",
)
path.write_text(text)
