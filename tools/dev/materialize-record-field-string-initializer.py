#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"expected exactly one match in {path}, found {text.count(old)}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast_global.c",
    '''    if (object->relocation_count != 0U) {\n        size_t relocation_index;\n\n        if (!minic_type_is_record(object->type)) {\n            return false;\n        }\n''',
    '''    if (object->relocation_count != 0U) {\n        size_t relocation_index;\n\n        if (!minic_type_is_record(object->type) && !minic_type_is_array(object->type)) {\n            return false;\n        }\n''',
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''            (object->relocation_count != 0U && !object->is_zero_initialized &&\n             (!minic_type_is_record(object->type) || object->initializer_count == 0U)) ||\n''',
    '''            (object->relocation_count != 0U && !object->is_zero_initialized &&\n             ((!minic_type_is_record(object->type) && !minic_type_is_array(object->type)) ||\n              object->initializer_count == 0U)) ||\n''',
)

replace_once(
    "src/target/riscv64/codegen_function.c",
    '''    } else if (minic_riscv64_record_array_info(program, object->type, NULL, NULL)) {\n        if (object->initializer_count == 0U || object->relocation_count != 0U) {\n            return false;\n        }\n''',
    '''    } else if (minic_riscv64_record_array_info(program, object->type, NULL, NULL)) {\n        if (object->initializer_count == 0U) {\n            return false;\n        }\n''',
)

replace_once(
    "src/target/riscv64/codegen_function.c",
    '''    if (minic_type_is_record(object->type) && object->initializer_count != 0U) {\n        if (!minic_riscv64_emit_record_values(file, program, object)) {\n            return false;\n        }\n    } else if (object->relocation_count != 0U) {\n        if (!emit_symbol_relocs(file, program, object)) {\n            return false;\n        }\n    } else if (object->is_zero_initialized || object->is_tentative) {\n        if (!minic_riscv64_emit_zero_bytes(file, storage_size)) {\n            return false;\n        }\n    } else if (minic_type_is_record(object->type)) {\n        if (!minic_riscv64_emit_record_values(file, program, object)) {\n            return false;\n        }\n    } else if (minic_riscv64_record_array_info(program, object->type, NULL, NULL)) {\n        if (!minic_riscv64_emit_record_array_values(file, program, object)) {\n            return false;\n        }\n''',
    '''    if (minic_type_is_record(object->type) && object->initializer_count != 0U) {\n        if (!minic_riscv64_emit_record_values(file, program, object)) {\n            return false;\n        }\n    } else if (minic_riscv64_record_array_info(program, object->type, NULL, NULL) &&\n               object->initializer_count != 0U) {\n        if (!minic_riscv64_emit_record_array_values(file, program, object)) {\n            return false;\n        }\n    } else if (object->relocation_count != 0U) {\n        if (!emit_symbol_relocs(file, program, object)) {\n            return false;\n        }\n    } else if (object->is_zero_initialized || object->is_tentative) {\n        if (!minic_riscv64_emit_zero_bytes(file, storage_size)) {\n            return false;\n        }\n    } else if (minic_type_is_record(object->type)) {\n        if (!minic_riscv64_emit_record_values(file, program, object)) {\n            return false;\n        }\n''',
)

parser_string = Path("src/frontend/parser_string.c")
text = parser_string.read_text()
anchor = '''bool minic_parser_get_predefined_function_name_object(MinicParser *parser,\n'''
if text.count(anchor) != 1:
    raise SystemExit("parser_string anchor mismatch")
helper = r'''bool minic_parser_add_bounded_string_literal_initializer(MinicParser *parser,
                                                         MinicGlobalObjectId object_id,
                                                         size_t element_capacity) {
    MinicParser probe;
    size_t decoded_length;
    size_t total_length;
    size_t stored_count;

    if (parser == NULL || element_capacity == 0U ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        return false;
    }
    probe = *parser;
    total_length = 0U;
    while (probe.current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!decoded_string_length(
                &probe, probe.current.span, probe.current.kind, &decoded_length) ||
            total_length > SIZE_MAX - decoded_length || !minic_parser_advance(&probe)) {
            return false;
        }
        total_length += decoded_length;
    }
    if (total_length > element_capacity) {
        minic_parser_error(parser, "string initializer is too long for character array");
        return false;
    }

    while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        MinicSourceSpan literal_span;

        literal_span = parser->current.span;
        if (!add_string_payload(parser, literal_span, MINIC_TOKEN_STRING_LITERAL, object_id) ||
            !minic_parser_advance(parser)) {
            return false;
        }
    }
    stored_count = total_length;
    if (stored_count < element_capacity) {
        if (!minic_c0_global_object_add_initializer(parser->program, object_id, 0)) {
            minic_parser_error(parser, "out of memory while terminating bounded string initializer");
            return false;
        }
        stored_count += 1U;
    }
    while (stored_count < element_capacity) {
        if (!minic_c0_global_object_add_initializer(parser->program, object_id, 0)) {
            minic_parser_error(parser, "out of memory while padding bounded string initializer");
            return false;
        }
        stored_count += 1U;
    }
    return true;
}

'''
parser_string.write_text(text.replace(anchor, helper + anchor, 1))

replace_once(
    "src/frontend/parser_internal.h",
    '''bool minic_parser_add_string_literal_initializer(MinicParser *parser,\n                                                 MinicGlobalObjectId object_id,\n                                                 size_t *element_count);\n''',
    '''bool minic_parser_add_string_literal_initializer(MinicParser *parser,\n                                                 MinicGlobalObjectId object_id,\n                                                 size_t *element_count);\nbool minic_parser_add_bounded_string_literal_initializer(MinicParser *parser,\n                                                         MinicGlobalObjectId object_id,\n                                                         size_t element_capacity);\n''',
)

replace_once(
    "src/frontend/parser_global.c",
    '''        } else {\n            if (!minic_parser_expect(\n                    parser, MINIC_TOKEN_LBRACE, "expected '{' in record field array initializer")) {\n                return false;\n            }\n            element_index = 0U;\n''',
    '''        } else if (minic_type_is_char_integer(field->type) &&\n                   parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {\n            if (!minic_parser_add_bounded_string_literal_initializer(\n                    parser, object_id, field->element_count)) {\n                return false;\n            }\n        } else {\n            if (!minic_parser_expect(\n                    parser, MINIC_TOKEN_LBRACE, "expected '{' in record field array initializer")) {\n                return false;\n            }\n            element_index = 0U;\n''',
)

fixture = Path("tests/compiler/c0/static_record_array.c")
text = fixture.read_text()
append = r'''

static int read_named(void) {
    return 17;
}

static int write_named(void) {
    return 23;
}

struct MiniNamedHook {
    char name[8];
    int (*read_u64)(void);
    int (*write_u64)(void);
};

static struct MiniNamedHook named_hooks[] = {
    {
        .name = "shares",
        .read_u64 = read_named,
        .write_u64 = write_named,
    },
};

struct MiniExactTag {
    char tag[3];
    int marker;
};

static struct MiniExactTag exact_tags[] = {
    {
        .tag = "abc",
        .marker = 7,
    },
};

int read_named_hook(void) {
    return named_hooks[0].name[0] + named_hooks[0].name[5] + named_hooks[0].read_u64() +
           named_hooks[0].write_u64() + exact_tags[0].tag[2] + exact_tags[0].marker;
}
'''
if "struct MiniNamedHook" in text:
    raise SystemExit("fixture already materialized")
fixture.write_text(text + append)

runner = Path("tests/compiler/c0/run-static-record-arrays.sh")
text = runner.read_text()
needle = '''grep -F 'read_sysctl_size:' "$work/static_record_array.s" >/dev/null\nprintf '%s\\n' 'PASS compiler/c0/static_record_array inferred-count=3 fields=2 missing-field-zero=1 size=6 complex-empty=1 complex-size=32 shared-owner=1 internal-rodata=1'\n'''
replacement = r'''grep -F 'read_sysctl_size:' "$work/static_record_array.s" >/dev/null
grep -F '.type named_hooks, @object' "$work/static_record_array.s" >/dev/null
grep -F '.size named_hooks, 24' "$work/static_record_array.s" >/dev/null
grep -F '  .byte 115' "$work/static_record_array.s" >/dev/null
grep -F '  .byte 104' "$work/static_record_array.s" >/dev/null
grep -F '  .byte 97' "$work/static_record_array.s" >/dev/null
grep -F '  .byte 114' "$work/static_record_array.s" >/dev/null
grep -F '  .byte 101' "$work/static_record_array.s" >/dev/null
grep -F '  .dword read_named' "$work/static_record_array.s" >/dev/null
grep -F '  .dword write_named' "$work/static_record_array.s" >/dev/null
grep -F '.type exact_tags, @object' "$work/static_record_array.s" >/dev/null
grep -F '.size exact_tags, 8' "$work/static_record_array.s" >/dev/null

cat >"$work/too_long.c" <<'EOF'
struct TooLongName {
    char name[3];
};
static struct TooLongName bad[] = {
    { .name = "abcd" },
};
EOF
"$host_cc" -E -P -x c "$work/too_long.c" -o "$work/too_long.i"
if "$minic" -S "$work/too_long.i" -o "$work/too_long.s" 2>"$work/too_long.err"; then
    echo 'overlong fixed character-array field string initializer was accepted' >&2
    exit 1
fi
grep -F 'string initializer is too long for character array' "$work/too_long.err" >/dev/null

printf '%s\n' 'PASS compiler/c0/static_record_array inferred-count=3 fields=2 missing-field-zero=1 size=6 complex-empty=1 complex-size=32 string-field=1 exact-fit=1 function-relocations=1 shared-owner=1 internal-rodata=1'
'''
if text.count(needle) != 1:
    raise SystemExit("runner anchor mismatch")
runner.write_text(text.replace(needle, replacement, 1))
