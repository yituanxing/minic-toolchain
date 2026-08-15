#!/usr/bin/env python3
from pathlib import Path


def replace_between(text: str, start: str, end: str, replacement: str, label: str) -> str:
    begin = text.find(start)
    if begin < 0:
        raise SystemExit(f"{label}: start marker not found")
    finish = text.find(end, begin)
    if finish < 0:
        raise SystemExit(f"{label}: end marker not found")
    return text[:begin] + replacement + text[finish:]


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return text.replace(old, new, 1)


# Frontend: retire the old integer-only record-array parser.  The probe only
# counts top-level initializer elements so an inferred array type is complete
# before the canonical static-storage initializer owner records semantic values.
path = Path("src/frontend/parser_global.c")
text = path.read_text()
new_record_array = r'''static bool count_static_array_initializer_elements(MinicParser *parser,
                                                    size_t *element_count) {
    MinicParser probe;
    size_t brace_depth;
    size_t parenthesis_depth;
    size_t bracket_depth;

    if (parser == NULL || element_count == NULL || parser->current.kind != MINIC_TOKEN_LBRACE) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    *element_count = 0U;
    if (probe.current.kind == MINIC_TOKEN_RBRACE) {
        return true;
    }

    *element_count = 1U;
    brace_depth = 0U;
    parenthesis_depth = 0U;
    bracket_depth = 0U;
    while (probe.current.kind != MINIC_TOKEN_EOF) {
        switch (probe.current.kind) {
        case MINIC_TOKEN_LBRACE:
            brace_depth += 1U;
            break;
        case MINIC_TOKEN_RBRACE:
            if (brace_depth == 0U) {
                return parenthesis_depth == 0U && bracket_depth == 0U;
            }
            brace_depth -= 1U;
            break;
        case MINIC_TOKEN_LPAREN:
            parenthesis_depth += 1U;
            break;
        case MINIC_TOKEN_RPAREN:
            if (parenthesis_depth == 0U) {
                return false;
            }
            parenthesis_depth -= 1U;
            break;
        case MINIC_TOKEN_LBRACKET:
            bracket_depth += 1U;
            break;
        case MINIC_TOKEN_RBRACKET:
            if (bracket_depth == 0U) {
                return false;
            }
            bracket_depth -= 1U;
            break;
        case MINIC_TOKEN_COMMA:
            if (brace_depth == 0U && parenthesis_depth == 0U && bracket_depth == 0U) {
                if (!minic_parser_advance(&probe)) {
                    return false;
                }
                if (probe.current.kind == MINIC_TOKEN_RBRACE) {
                    return true;
                }
                if (*element_count == SIZE_MAX) {
                    return false;
                }
                *element_count += 1U;
                continue;
            }
            break;
        default:
            break;
        }
        if (!minic_parser_advance(&probe)) {
            return false;
        }
    }
    return false;
}

static bool
parse_static_record_array(MinicParser *parser, MinicType element_type, MinicSourceSpan name_span) {
    const MinicRecord *record;
    MinicType object_type;
    MinicGlobalObjectId object_id;
    size_t declared_count;
    size_t initializer_count;
    bool inferred_bound;

    record = minic_c0_program_record(parser->program, element_type.record_id);
    if (record == NULL || !record->is_complete || record->is_union || record->field_count == 0U) {
        minic_parser_error(parser, "static record array requires a complete non-empty struct type");
        return false;
    }

    declared_count = 0U;
    inferred_bound = false;
    if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACKET, "expected '['")) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
        inferred_bound = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (!minic_parser_parse_fixed_array_bound(parser, &declared_count)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "multi-dimensional static record arrays are not supported yet");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static record array") ||
        parser->current.kind != MINIC_TOKEN_LBRACE ||
        !count_static_array_initializer_elements(parser, &initializer_count)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot inspect static record array initializer");
        }
        return false;
    }
    if (inferred_bound) {
        if (initializer_count == 0U) {
            minic_parser_error(parser, "cannot infer static record array bound from an empty initializer");
            return false;
        }
        declared_count = initializer_count;
    } else if (initializer_count > declared_count) {
        minic_parser_error(parser, "too many static record array initializers");
        return false;
    }

    if (!minic_c0_program_add_array_type(
            parser->program, element_type, declared_count, &object_type) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id) ||
        !minic_parser_parse_static_storage_initializer_value(parser, object_id, object_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse static record array initializer");
        }
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after static record array");
}

'''
text = replace_between(
    text,
    "static bool static_record_array_append_value(",
    "static bool parse_static_record(",
    new_record_array,
    "record-array parser",
)
path.write_text(text)

# Backend: keep record-array classification narrow, but emit its already-normalized
# initializer through the same recursive constant emitter used by nested records.
path = Path("src/target/riscv64/codegen_function.c")
text = path.read_text()
new_record_array_emitter = r'''static bool minic_riscv64_emit_record_array_values(FILE *file,
                                                   const MinicC0Program *program,
                                                   const MinicGlobalObject *object) {
    size_t object_alignment;
    size_t storage_size;
    size_t emitted_size;
    size_t initializer_index;
    size_t relocation_index;

    if (file == NULL || program == NULL || object == NULL || object->is_zero_initialized ||
        !minic_riscv64_record_array_info(program, object->type, NULL, NULL) ||
        !minic_data_layout_global_object(
            minic_default_data_layout(), program, object, &storage_size, &object_alignment)) {
        return false;
    }
    (void)object_alignment;
    initializer_index = 0U;
    relocation_index = 0U;
    emitted_size = 0U;
    return minic_riscv64_emit_constant_value(file,
                                             program,
                                             object,
                                             object->type,
                                             &initializer_index,
                                             &relocation_index,
                                             &emitted_size) &&
           initializer_index == object->initializer_count &&
           relocation_index == object->relocation_count && emitted_size == storage_size;
}

'''
text = replace_between(
    text,
    "static bool minic_riscv64_emit_record_array_values(",
    "static bool minic_riscv64_emit_file_asm(",
    new_record_array_emitter,
    "record-array emitter",
)
old_validation = '''    } else if (minic_riscv64_record_array_info(program, object->type, NULL, NULL)) {\n        const MinicArrayType *array_type;\n        const MinicRecord *record;\n\n        if (!minic_riscv64_record_array_info(program, object->type, &array_type, &record) ||\n            record->field_count == 0U ||\n            array_type->element_count > SIZE_MAX / record->field_count ||\n            object->relocation_count != 0U ||\n            object->initializer_count != array_type->element_count * record->field_count) {\n            return false;\n        }\n'''
new_validation = '''    } else if (minic_riscv64_record_array_info(program, object->type, NULL, NULL)) {\n        if (object->initializer_count == 0U || object->relocation_count != 0U) {\n            return false;\n        }\n'''
text = replace_once(text, old_validation, new_validation, "record-array backend validation")
path.write_text(text)

# Permanent regression: preserve the original compact integer record-array case
# and add the unchanged-Linux-shaped complex record with an empty sentinel.
path = Path("tests/compiler/c0/static_record_array.c")
text = path.read_text()
append = r'''

typedef unsigned short MiniMode;

struct MiniCtlTable {
    const char *procname;
    void *data;
    int maxlen;
    MiniMode mode;
    int (*proc_handler)(void);
};

static struct MiniCtlTable sched_core_sysctls_like[] = {
    {}
};

int read_sysctl_size(void) {
    return (int)sizeof(sched_core_sysctls_like);
}
'''
if "sched_core_sysctls_like" in text:
    raise SystemExit("static record-array regression already materialized")
path.write_text(text.rstrip() + append)

path = Path("tests/compiler/c0/run-static-record-arrays.sh")
text = path.read_text()
old = '''if grep -F '.globl priority' "$work/static_record_array.s" >/dev/null; then\n    echo 'static record array leaked external linkage' >&2\n    exit 1\nfi\ngrep -F 'read_priority:' "$work/static_record_array.s" >/dev/null\nprintf '%s\\n' 'PASS compiler/c0/static_record_array inferred-count=3 fields=2 missing-field-zero=1 size=6 internal-rodata=1'\n'''
new = '''if grep -F '.globl priority' "$work/static_record_array.s" >/dev/null; then\n    echo 'static record array leaked external linkage' >&2\n    exit 1\nfi\ngrep -F 'read_priority:' "$work/static_record_array.s" >/dev/null\ngrep -F '.type sched_core_sysctls_like, @object' "$work/static_record_array.s" >/dev/null\ngrep -F '.size sched_core_sysctls_like, 32' "$work/static_record_array.s" >/dev/null\nif grep -F '.globl sched_core_sysctls_like' "$work/static_record_array.s" >/dev/null; then\n    echo 'complex static record array leaked external linkage' >&2\n    exit 1\nfi\ngrep -F 'read_sysctl_size:' "$work/static_record_array.s" >/dev/null\nprintf '%s\\n' 'PASS compiler/c0/static_record_array inferred-count=3 fields=2 missing-field-zero=1 size=6 complex-empty=1 complex-size=32 shared-owner=1 internal-rodata=1'\n'''
text = replace_once(text, old, new, "static record-array runner")
path.write_text(text)
