from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"anchor mismatch {path}: {count}: {old[:80]!r}")
    p.write_text(text.replace(old, new, 1))


def replace_region(path: str, start_marker: str, end_marker: str, replacement: str) -> None:
    p = Path(path)
    text = p.read_text()
    start = text.find(start_marker)
    end = text.find(end_marker, start + len(start_marker))
    if start < 0 or end < 0:
        raise SystemExit(f"region mismatch {path}: start={start} end={end}")
    p.write_text(text[:start] + replacement + text[end:])


# Token model: keep wide literals distinct so existing narrow-only semantic consumers fail closed.
replace_once(
    "src/frontend/token.h",
    "    MINIC_TOKEN_STRING_LITERAL,\n    MINIC_TOKEN_PREPROCESSOR_DIRECTIVE,\n",
    "    MINIC_TOKEN_STRING_LITERAL,\n    MINIC_TOKEN_WIDE_STRING_LITERAL,\n    MINIC_TOKEN_PREPROCESSOR_DIRECTIVE,\n",
)
replace_once(
    "src/frontend/token.c",
    "    case MINIC_TOKEN_STRING_LITERAL:\n        return \"string literal\";\n",
    "    case MINIC_TOKEN_STRING_LITERAL:\n        return \"string literal\";\n    case MINIC_TOKEN_WIDE_STRING_LITERAL:\n        return \"wide string literal\";\n",
)

# Lexer: recognize L\"...\" before generic identifiers. Lvalue remains an identifier.
replace_region(
    "src/frontend/lexer.c",
    "static bool minic_lexer_scan_string_literal(",
    "static bool minic_lexer_scan_character_constant(",
    '''static bool minic_lexer_scan_string_literal(MinicLexer *lexer,
                                            MinicToken *token,
                                            MinicDiagnostic *diagnostic,
                                            MinicSourcePosition begin,
                                            MinicTokenKind kind) {
    if (kind == MINIC_TOKEN_WIDE_STRING_LITERAL) {
        if (minic_lexer_peek(lexer) != 'L' || minic_lexer_peek_next(lexer) != '"') {
            return false;
        }
        minic_lexer_advance(lexer);
    } else if (kind != MINIC_TOKEN_STRING_LITERAL || minic_lexer_peek(lexer) != '"') {
        return false;
    }

    minic_lexer_advance(lexer);
    for (;;) {
        char character;

        character = minic_lexer_peek(lexer);
        if (character == '"') {
            minic_lexer_advance(lexer);
            token->kind = kind;
            token->span.end = minic_lexer_position(lexer);
            return true;
        }
        if (character == '\\0') {
            token->span.end = minic_lexer_position(lexer);
            minic_lexer_set_message(lexer, diagnostic, begin, "unterminated string literal");
            return false;
        }
        if (character == '\\n' || character == '\\r') {
            token->span.end = minic_lexer_position(lexer);
            minic_lexer_set_message(
                lexer, diagnostic, minic_lexer_position(lexer), "newline in string literal");
            return false;
        }
        if (character == '\\\\') {
            minic_lexer_advance(lexer);
            character = minic_lexer_peek(lexer);
            if (character == '\\0') {
                token->span.end = minic_lexer_position(lexer);
                minic_lexer_set_message(lexer, diagnostic, begin, "unterminated string literal");
                return false;
            }
            if (character == '\\n' || character == '\\r') {
                token->span.end = minic_lexer_position(lexer);
                minic_lexer_set_message(
                    lexer, diagnostic, minic_lexer_position(lexer), "newline in string literal");
                return false;
            }
        }
        minic_lexer_advance(lexer);
    }
}

''',
)
replace_once(
    "src/frontend/lexer.c",
    "    if (minic_is_identifier_start(character)) {\n",
    "    if (character == 'L' && minic_lexer_peek_next(lexer) == '\"') {\n"
    "        return minic_lexer_scan_string_literal(\n"
    "            lexer, token, diagnostic, begin, MINIC_TOKEN_WIDE_STRING_LITERAL);\n"
    "    }\n\n"
    "    if (minic_is_identifier_start(character)) {\n",
)
replace_once(
    "src/frontend/lexer.c",
    "        return minic_lexer_scan_string_literal(lexer, token, diagnostic, begin);\n",
    "        return minic_lexer_scan_string_literal(\n"
    "            lexer, token, diagnostic, begin, MINIC_TOKEN_STRING_LITERAL);\n",
)

# Effective target compilation profile: Linux/RV64 uses -fshort-wchar, so expose the type via TargetInfo.
replace_once(
    "src/target/target_info.h",
    "    const MinicDataLayout *data_layout;\n    bool gnu_sizeof_void_is_one;\n",
    "    const MinicDataLayout *data_layout;\n    MinicType wide_character_type;\n    bool gnu_sizeof_void_is_one;\n",
)
replace_once(
    "src/target/target_info.h",
    "const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target);\n",
    "const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target);\n"
    "bool minic_target_info_wide_character_type(const MinicTargetInfo *target, MinicType *type);\n",
)
replace_once(
    "src/target/target_info.c",
    "        target.data_layout = minic_default_data_layout();\n        target.gnu_sizeof_void_is_one = true;\n",
    "        target.data_layout = minic_default_data_layout();\n"
    "        target.wide_character_type = minic_type_unsigned_short();\n"
    "        target.gnu_sizeof_void_is_one = true;\n",
)
replace_once(
    "src/target/target_info.c",
    "const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target) {\n"
    "    return target == NULL ? NULL : target->data_layout;\n"
    "}\n\n",
    "const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target) {\n"
    "    return target == NULL ? NULL : target->data_layout;\n"
    "}\n\n"
    "bool minic_target_info_wide_character_type(const MinicTargetInfo *target, MinicType *type) {\n"
    "    if (target == NULL || type == NULL || !minic_type_is_integer(target->wide_character_type)) {\n"
    "        return false;\n"
    "    }\n"
    "    *type = target->wide_character_type;\n"
    "    return true;\n"
    "}\n\n",
)

# Expression entry points opt in to wide literals; other narrow-only string consumers remain unchanged.
replace_once(
    "src/frontend/parser_expression.c",
    "    case MINIC_TOKEN_STRING_LITERAL:\n    case MINIC_TOKEN_LPAREN:\n",
    "    case MINIC_TOKEN_STRING_LITERAL:\n    case MINIC_TOKEN_WIDE_STRING_LITERAL:\n    case MINIC_TOKEN_LPAREN:\n",
)
p = Path("src/frontend/parser_expression.c")
text = p.read_text()
start = text.find("static bool parse_primary(")
end = text.find("static bool current_is_sizeof(", start)
if start < 0 or end < 0:
    raise SystemExit("parse_primary region mismatch")
region = text[start:end]
old = "    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {\n"
new = "    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL ||\n        parser->current.kind == MINIC_TOKEN_WIDE_STRING_LITERAL) {\n"
if region.count(old) != 1:
    raise SystemExit(f"parse_primary string anchor mismatch: {region.count(old)}")
region = region.replace(old, new, 1)
p.write_text(text[:start] + region + text[end:])

# Legacy array-bound sizeof path must count wide string storage bytes, not code units.
p = Path("src/frontend/parser_core.c")
text = p.read_text()
start = text.find("static bool parse_array_bound_sizeof(")
end = text.find("static bool array_bound_parenthesis_starts_integer_cast(", start)
if start < 0 or end < 0:
    raise SystemExit("parse_array_bound_sizeof region mismatch")
region = text[start:end]
old = "    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {\n"
new = "    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL ||\n        parser->current.kind == MINIC_TOKEN_WIDE_STRING_LITERAL) {\n"
if region.count(old) != 1:
    raise SystemExit(f"array-bound sizeof string anchor mismatch: {region.count(old)}")
region = region.replace(old, new, 1)
p.write_text(text[:start] + region + text[end:])

# String semantic core: homogeneous narrow/wide concatenation and target-owned element type.
p = Path("src/frontend/parser_string.c")
text = p.read_text()
insert_after = '''static int hex_digit_value(char character) {
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

'''
helpers = insert_after + '''static bool string_literal_kind(MinicTokenKind kind) {
    return kind == MINIC_TOKEN_STRING_LITERAL || kind == MINIC_TOKEN_WIDE_STRING_LITERAL;
}

static bool string_literal_payload_bounds(const MinicParser *parser,
                                          MinicSourceSpan span,
                                          MinicTokenKind kind,
                                          size_t *cursor,
                                          size_t *end) {
    size_t prefix_length;

    if (parser == NULL || cursor == NULL || end == NULL || !string_literal_kind(kind)) {
        return false;
    }
    prefix_length = kind == MINIC_TOKEN_WIDE_STRING_LITERAL ? 2U : 1U;
    if (span.end.offset <= span.begin.offset + prefix_length ||
        parser->source[span.end.offset - 1U] != '"') {
        return false;
    }
    if (kind == MINIC_TOKEN_WIDE_STRING_LITERAL) {
        if (parser->source[span.begin.offset] != 'L' || parser->source[span.begin.offset + 1U] != '"') {
            return false;
        }
    } else if (parser->source[span.begin.offset] != '"') {
        return false;
    }
    *cursor = span.begin.offset + prefix_length;
    *end = span.end.offset - 1U;
    return true;
}

static bool string_literal_element_type(MinicParser *parser,
                                        MinicTokenKind kind,
                                        MinicType *type) {
    if (parser == NULL || type == NULL) {
        return false;
    }
    if (kind == MINIC_TOKEN_STRING_LITERAL) {
        *type = minic_type_char();
        return true;
    }
    return kind == MINIC_TOKEN_WIDE_STRING_LITERAL &&
           minic_target_info_wide_character_type(parser->target_info, type);
}

'''
if text.count(insert_after) != 1:
    raise SystemExit(f"hex helper anchor mismatch: {text.count(insert_after)}")
text = text.replace(insert_after, helpers, 1)
p.write_text(text)

replace_region(
    "src/frontend/parser_string.c",
    "static bool decoded_string_length(",
    "bool minic_parser_parse_string_literal_size(",
    '''static bool decoded_string_length(MinicParser *parser,
                                  MinicSourceSpan span,
                                  MinicTokenKind kind,
                                  size_t *length) {
    size_t cursor;
    size_t end;
    size_t result;

    if (parser == NULL || length == NULL ||
        !string_literal_payload_bounds(parser, span, kind, &cursor, &end)) {
        return false;
    }
    result = 0U;
    while (cursor < end) {
        if (parser->source[cursor] == '\\\\') {
            int value;

            cursor += 1U;
            if (!decode_string_escape(parser->source, &cursor, end, &value)) {
                minic_parser_error(parser, "unsupported string escape");
                return false;
            }
        } else {
            cursor += 1U;
        }
        if (result == SIZE_MAX) {
            minic_parser_error(parser, "string literal is too long");
            return false;
        }
        result += 1U;
    }
    *length = result;
    return true;
}

''',
)
replace_region(
    "src/frontend/parser_string.c",
    "bool minic_parser_parse_string_literal_size(",
    "static bool\nadd_string_payload(",
    '''bool minic_parser_parse_string_literal_size(MinicParser *parser, uint64_t *size) {
    MinicTokenKind literal_kind;
    MinicType element_type;
    size_t decoded_length;
    size_t element_size;
    size_t total_length;

    if (parser == NULL || size == NULL || !string_literal_kind(parser->current.kind)) {
        return false;
    }
    literal_kind = parser->current.kind;
    if (!string_literal_element_type(parser, literal_kind, &element_type)) {
        return false;
    }
    total_length = 0U;
    while (string_literal_kind(parser->current.kind)) {
        if (parser->current.kind != literal_kind) {
            minic_parser_error(parser, "mixed string literal encodings are not supported yet");
            return false;
        }
        if (!decoded_string_length(
                parser, parser->current.span, parser->current.kind, &decoded_length) ||
            total_length > SIZE_MAX - decoded_length) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "concatenated string literal is too long");
            }
            return false;
        }
        total_length += decoded_length;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (total_length == SIZE_MAX ||
        !minic_target_info_sizeof_type(
            parser->target_info, parser->program, element_type, &element_size) ||
        element_size == 0U || (uint64_t)(total_length + 1U) > UINT64_MAX / element_size) {
        minic_parser_error(parser, "string literal sizeof result is too large");
        return false;
    }
    *size = (uint64_t)(total_length + 1U) * (uint64_t)element_size;
    return true;
}

''',
)
replace_region(
    "src/frontend/parser_string.c",
    "static bool\nadd_string_payload(",
    "bool minic_parser_add_string_literal_initializer(",
    '''static bool add_string_payload(MinicParser *parser,
                               MinicSourceSpan span,
                               MinicTokenKind kind,
                               MinicGlobalObjectId object_id) {
    size_t cursor;
    size_t end;

    if (!string_literal_payload_bounds(parser, span, kind, &cursor, &end)) {
        return false;
    }
    while (cursor < end) {
        int value;

        if (parser->source[cursor] == '\\\\') {
            cursor += 1U;
            if (!decode_string_escape(parser->source, &cursor, end, &value)) {
                minic_parser_error(parser, "unsupported string escape");
                return false;
            }
        } else {
            value = (int)(unsigned char)parser->source[cursor];
            cursor += 1U;
        }
        if (!minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            minic_parser_error(parser, "out of memory while storing string literal");
            return false;
        }
    }
    return true;
}

''',
)

# Existing character-array initializer and asm-text APIs remain deliberately narrow-only.
p = Path("src/frontend/parser_string.c")
text = p.read_text()
text = text.replace(
    "decoded_string_length(&probe, probe.current.span, &decoded_length)",
    "decoded_string_length(&probe, probe.current.span, probe.current.kind, &decoded_length)",
)
text = text.replace(
    "add_string_payload(parser, literal_span, object_id)",
    "add_string_payload(parser, literal_span, MINIC_TOKEN_STRING_LITERAL, object_id)",
)
p.write_text(text)

replace_region(
    "src/frontend/parser_string.c",
    "bool minic_parser_create_string_literal_object(",
    "bool minic_parser_parse_string_text(",
    '''bool minic_parser_create_string_literal_object(MinicParser *parser,
                                               MinicGlobalObjectId *object_id,
                                               MinicType *array_type,
                                               MinicSourceSpan *span) {
    MinicParser probe;
    MinicTokenKind literal_kind;
    MinicType element_type;
    char object_name[64];
    int object_name_length;
    size_t decoded_length;
    size_t total_length;
    MinicSourceSpan combined_span;

    if (parser == NULL || object_id == NULL || array_type == NULL || span == NULL ||
        !string_literal_kind(parser->current.kind)) {
        return false;
    }

    literal_kind = parser->current.kind;
    if (!string_literal_element_type(parser, literal_kind, &element_type)) {
        minic_parser_error(parser, "unsupported string literal element type");
        return false;
    }
    probe = *parser;
    combined_span = probe.current.span;
    total_length = 0U;
    while (string_literal_kind(probe.current.kind)) {
        if (probe.current.kind != literal_kind) {
            minic_parser_error(parser, "mixed string literal encodings are not supported yet");
            return false;
        }
        if (!decoded_string_length(
                &probe, probe.current.span, probe.current.kind, &decoded_length) ||
            total_length > SIZE_MAX - decoded_length) {
            if (probe.diagnostic != NULL && probe.diagnostic->message[0] == '\\0') {
                minic_parser_error(&probe, "concatenated string literal is too long");
            }
            return false;
        }
        total_length += decoded_length;
        combined_span.end = probe.current.span.end;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
    }
    if (total_length == SIZE_MAX ||
        !minic_c0_program_add_array_type(
            parser->program, element_type, total_length + 1U, array_type)) {
        minic_parser_error(parser, "cannot build string literal array type");
        return false;
    }

    object_name_length = snprintf(object_name,
                                  sizeof(object_name),
                                  ".Lminic_string_%zu",
                                  parser->program->global_object_count);
    if (object_name_length <= 0 || (size_t)object_name_length >= sizeof(object_name) ||
        !minic_c0_program_add_global_object(parser->program,
                                            object_name,
                                            (size_t)object_name_length,
                                            *array_type,
                                            true,
                                            true,
                                            object_id)) {
        minic_parser_error(parser, "cannot create string literal object");
        return false;
    }

    while (string_literal_kind(parser->current.kind)) {
        MinicSourceSpan literal_span;

        if (parser->current.kind != literal_kind) {
            minic_parser_error(parser, "mixed string literal encodings are not supported yet");
            return false;
        }
        literal_span = parser->current.span;
        if (!add_string_payload(parser, literal_span, parser->current.kind, *object_id) ||
            !minic_parser_advance(parser)) {
            return false;
        }
    }
    if (!minic_c0_global_object_add_initializer(parser->program, *object_id, 0)) {
        minic_parser_error(parser, "out of memory while terminating string literal");
        return false;
    }
    *span = combined_span;
    return true;
}

''',
)

# Any remaining decoded_string_length call in narrow-only parse_string_text needs the token kind.
p = Path("src/frontend/parser_string.c")
text = p.read_text()
text = text.replace(
    "decoded_string_length(&probe, probe.current.span, &decoded_length)",
    "decoded_string_length(&probe, probe.current.span, probe.current.kind, &decoded_length)",
)
p.write_text(text)

# Preserve nested expression diagnostics in indirect calls instead of overwriting them as arity errors.
replace_once(
    "src/frontend/parser_postfix.c",
    '''        if (parser->current.kind == MINIC_TOKEN_RPAREN ||
            !minic_parser_parse_expression(parser, &argument_id, 0U)) {
            indirect_argument_count_error(parser);
            return false;
        }
''',
    '''        if (parser->current.kind == MINIC_TOKEN_RPAREN) {
            indirect_argument_count_error(parser);
            return false;
        }
        if (!minic_parser_parse_expression(parser, &argument_id, 0U)) {
            return false;
        }
''',
)

# Lexer coverage for the exact prefix boundary.
p = Path("tests/frontend/lexer_test.c")
text = p.read_text()
needle = '''static int test_invalid_string_literals(void)
'''
addition = r'''static int test_wide_string_literals(void)
{
    static const char source[] = "L\"SecureBoot\" Lvalue";
    MinicLexer lexer;

    minic_lexer_initialize(&lexer, "wide-strings.c", source, sizeof(source) - 1U);
    if (expect_token(&lexer, MINIC_TOKEN_WIDE_STRING_LITERAL, 1U, 1U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 15U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_EOF, 1U, 21U) != 0) {
        return 1;
    }
    return 0;
}

static int test_invalid_string_literals(void)
'''
if text.count(needle) != 1:
    raise SystemExit(f"lexer insertion anchor mismatch: {text.count(needle)}")
text = text.replace(needle, addition, 1)
old_main = '''        test_floating_constants() != 0 ||
        test_string_literals() != 0 ||
        test_invalid_string_literals() != 0 ||
'''
new_main = '''        test_floating_constants() != 0 ||
        test_string_literals() != 0 ||
        test_wide_string_literals() != 0 ||
        test_invalid_string_literals() != 0 ||
'''
if text.count(old_main) != 1:
    raise SystemExit(f"lexer main anchor mismatch: {text.count(old_main)}")
p.write_text(text.replace(old_main, new_main, 1))

# Linux-shaped semantic fixture and explicit fail-closed/diagnostic boundaries.
Path("tests/compiler/c0/wide_string_literal.c").write_text(r'''typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long efi_status_t;
typedef u16 efi_char16_t;

typedef struct {
    u8 bytes[16];
} efi_guid_t;

typedef efi_status_t efi_get_variable_t(efi_char16_t *name,
                                        efi_guid_t *vendor,
                                        u32 *attr,
                                        unsigned long *data_size,
                                        void *data);

_Static_assert(sizeof(L"A") == 4, "-fshort-wchar wide literal size");

static efi_status_t wide_efi_call(efi_get_variable_t *get_var)
{
    unsigned long size = 1;
    u8 secboot = 0;

    return get_var(L"SecureBoot", (efi_guid_t *)0, (u32 *)0, &size, &secboot);
}

int Lvalue_boundary(int Lvalue)
{
    return Lvalue + (int)wide_efi_call;
}
''')
Path("tests/compiler/c0/invalid_indirect_nested_diagnostic.c").write_text(r'''typedef int unary_fn_t(int value);

int bad_indirect_argument(unary_fn_t *fn)
{
    return fn(missing_name);
}
''')
Path("tests/compiler/c0/invalid_mixed_string_encoding.c").write_text(r'''int mixed_string_encoding(void)
{
    return (L"wide" "narrow")[0];
}
''')
Path("tests/compiler/c0/run-wide-string-literal.sh").write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-wide-string

mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -fshort-wchar -funsigned-char -x c \
    "$root/tests/compiler/c0/wide_string_literal.c" \
    -o "$work/wide_string_literal.i"
"$minic" -S "$work/wide_string_literal.i" -o "$work/wide_string_literal.s"

grep -F 'wide_efi_call:' "$work/wide_string_literal.s" >/dev/null
grep -F 'Lvalue_boundary:' "$work/wide_string_literal.s" >/dev/null
grep -F '  .half 83' "$work/wide_string_literal.s" >/dev/null
grep -F '  .half 0' "$work/wide_string_literal.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/wide_string_literal encoding=L element=unsigned-short sizeof=target-aware indirect-call=typed'

expect_failure() {
    name=$1
    message=$2

    "$host_cc" -E -P -std=gnu11 -fshort-wchar -funsigned-char -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    if ! grep -F "$message" "$work/$name.stderr" >/dev/null; then
        printf '%s\n' "FAIL compiler/c0/$name: diagnostic mismatch" >&2
        cat "$work/$name.stderr" >&2
        exit 1
    fi
    printf '%s\n' "PASS compiler/c0/$name"
}

expect_failure invalid_indirect_nested_diagnostic 'use of undeclared local'
expect_failure invalid_mixed_string_encoding 'mixed string literal encodings are not supported yet'
''')

# Freeze the focused gate into formal Compiler C0.
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    '''switch_control_flow_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    HOST_CC=cc \\
    BUILD_DIR="$root/build/ci-switch-control-flow" \\
        sh tests/compiler/c0/run-switch-control-flow.sh
}

''',
    '''switch_control_flow_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    HOST_CC=cc \\
    BUILD_DIR="$root/build/ci-switch-control-flow" \\
        sh tests/compiler/c0/run-switch-control-flow.sh
}

wide_string_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    HOST_CC=cc \\
    BUILD_DIR="$root/build/ci-wide-string" \\
        sh tests/compiler/c0/run-wide-string-literal.sh
}

''',
)
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "    'Phase 2: focused declaration/static-local/variadic-call/pointer-equality/switch/linenoise/SDS/RV64 suites, differential programs, tiny-AES, and cJSON'\n",
    "    'Phase 2: focused declaration/static-local/variadic-call/pointer-equality/switch/wide-string/linenoise/SDS/RV64 suites, differential programs, tiny-AES, and cJSON'\n",
)
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "start_gate switch-control-flow-focused switch_control_flow_focused\nstart_gate linenoise-driven-focused linenoise_driven_focused\n",
    "start_gate switch-control-flow-focused switch_control_flow_focused\n"
    "start_gate wide-string-focused wide_string_focused\n"
    "start_gate linenoise-driven-focused linenoise_driven_focused\n",
)
