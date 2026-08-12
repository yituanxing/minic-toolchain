#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def replace_once(path: str, old: str, new: str) -> None:
    p = ROOT / path
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


# Add a declaration-only function entity helper so both explicit `(params)`
# declarations and declarations whose final declarator type is already FUNCTION
# use the same Program identity/redeclaration/metadata path.
anchor = '''static bool parse_function(MinicParser *parser, bool is_internal) {
'''
helper = '''static bool finish_function_declaration_entity(
    MinicParser *parser,
    MinicSourceSpan name_span,
    MinicType return_type,
    const MinicType *parameter_types,
    size_t parameter_count,
    bool is_variadic,
    bool is_internal,
    const char *assembler_name,
    size_t assembler_name_length,
    bool has_assembler_name,
    MinicSymbolVisibility visibility,
    bool has_visibility,
    const char *section_name,
    size_t section_name_length,
    bool has_section) {
    MinicFunctionId function_id;
    const MinicFunction *existing_function;

    if (parser == NULL || parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        (parameter_count != 0U && parameter_types == NULL) ||
        parser->current.kind != MINIC_TOKEN_SEMICOLON) {
        return false;
    }
    function_id = minic_parser_find_function(parser, name_span);
    if (function_id != MINIC_FUNCTION_INVALID) {
        existing_function = minic_c0_program_function(parser->program, function_id);
        if (!minic_parser_function_signature_matches(
                existing_function, return_type, parameter_types, parameter_count, is_variadic) ||
            (!existing_function->is_internal && is_internal)) {
            minic_parser_error(parser, "conflicting function declaration");
            return false;
        }
        if (existing_function->is_internal) {
            is_internal = true;
        }
    } else {
        if (minic_parser_find_global_object_entity(parser, name_span) !=
                MINIC_GLOBAL_OBJECT_INVALID ||
            !minic_c0_program_add_function(parser->program,
                                           parser->source + name_span.begin.offset,
                                           minic_parser_span_length(name_span),
                                           parser->program->local_count,
                                           0U,
                                           MINIC_BLOCK_INVALID,
                                           &function_id) ||
            !minic_c0_program_set_function_signature(
                parser->program, function_id, return_type, parameter_types, parameter_count) ||
            !minic_c0_program_set_function_internal(parser->program, function_id, is_internal) ||
            !minic_c0_program_set_function_variadic(parser->program, function_id, is_variadic)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot declare function entity");
            }
            return false;
        }
    }
    if (has_assembler_name &&
        !minic_c0_program_set_function_assembler_name(
            parser->program, function_id, assembler_name, assembler_name_length)) {
        minic_parser_error(parser, "conflicting or invalid GNU function asm label");
        return false;
    }
    if (has_visibility &&
        !minic_c0_program_set_function_visibility(parser->program, function_id, visibility)) {
        minic_parser_error(parser, "conflicting GNU function visibility");
        return false;
    }
    if (has_section &&
        !minic_c0_program_set_function_section(
            parser->program, function_id, section_name, section_name_length)) {
        minic_parser_error(parser, "conflicting or invalid GNU function section");
        return false;
    }
    return minic_parser_advance(parser);
}

static bool parse_function(MinicParser *parser, bool is_internal) {
'''
replace_once("src/frontend/parser_function.c", anchor, helper)

# Route a final FUNCTION type before the old surface-shape object/function split.
anchor = '''    if (is_static_declaration && parser->current.kind != MINIC_TOKEN_LPAREN) {
'''
route = '''    if (!is_function_pointer_object && minic_type_is_function(return_type)) {
        const MinicFunctionType *function_type;
        MinicType typed_return_type;
        MinicType typed_parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
        size_t typed_parameter_count;
        size_t parameter_index;

        if (parser->current.kind == MINIC_TOKEN_LPAREN) {
            minic_parser_error(
                parser, "function-typed declarator cannot add another function suffix");
            return false;
        }
        function_type =
            minic_c0_program_function_type(parser->program, return_type.function_type_id);
        if (function_type == NULL || function_type->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS) {
            minic_parser_error(parser, "invalid function-typed declarator signature");
            return false;
        }
        /* FunctionType is Program-owned growable storage. Snapshot its canonical
         * signature before any subsequent semantic operation can grow owner pools. */
        typed_return_type = function_type->return_type;
        typed_parameter_count = function_type->parameter_count;
        for (parameter_index = 0U; parameter_index < typed_parameter_count; ++parameter_index) {
            typed_parameter_types[parameter_index] = function_type->parameter_types[parameter_index];
        }
        if (!apply_function_attribute_list(
                parser,
                &deferred_attributes,
                true,
                is_internal,
                is_inline,
                section_name,
                sizeof(section_name),
                &section_name_length,
                &has_section,
                "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes "
                "must be implemented explicitly") ||
            !parse_gnu_function_asm_label(parser,
                                          assembler_name,
                                          sizeof(assembler_name),
                                          &assembler_name_length,
                                          &has_assembler_name) ||
            !minic_parser_parse_gnu_function_attributes(parser)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_SEMICOLON) {
            minic_parser_error(parser,
                               "function-typed declarators currently support declarations only");
            return false;
        }
        return finish_function_declaration_entity(parser,
                                                  name_span,
                                                  typed_return_type,
                                                  typed_parameter_types,
                                                  typed_parameter_count,
                                                  false,
                                                  is_internal,
                                                  assembler_name,
                                                  assembler_name_length,
                                                  has_assembler_name,
                                                  visibility,
                                                  has_visibility,
                                                  section_name,
                                                  section_name_length,
                                                  has_section);
    }
    if (is_static_declaration && parser->current.kind != MINIC_TOKEN_LPAREN) {
'''
replace_once("src/frontend/parser_function.c", anchor, route)

# Reuse the same entity materialization helper for ordinary declaration-only
# function declarators instead of maintaining two registration paths.
old = '''    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (function_id == MINIC_FUNCTION_INVALID) {
            if (!minic_c0_program_add_function(parser->program,
                                               parser->source + name_span.begin.offset,
                                               minic_parser_span_length(name_span),
                                               parser->program->local_count,
                                               0U,
                                               MINIC_BLOCK_INVALID,
                                               &function_id) ||
                !minic_c0_program_set_function_signature(
                    parser->program, function_id, return_type, parameter_types, parameter_count) ||
                !minic_c0_program_set_function_internal(
                    parser->program, function_id, is_internal) ||
                !minic_c0_program_set_function_variadic(
                    parser->program, function_id, is_variadic)) {
                minic_parser_error(parser, "out of memory while declaring function");
                return false;
            }
        }
        if (has_assembler_name &&
            !minic_c0_program_set_function_assembler_name(
                parser->program, function_id, assembler_name, assembler_name_length)) {
            minic_parser_error(parser, "conflicting or invalid GNU function asm label");
            return false;
        }
        if (has_visibility &&
            !minic_c0_program_set_function_visibility(parser->program, function_id, visibility)) {
            minic_parser_error(parser, "conflicting GNU function visibility");
            return false;
        }
        if (has_section && !minic_c0_program_set_function_section(
                               parser->program, function_id, section_name, section_name_length)) {
            minic_parser_error(parser, "conflicting or invalid GNU function section");
            return false;
        }
        return minic_parser_advance(parser);
    }
'''
new = '''    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        return finish_function_declaration_entity(parser,
                                                  name_span,
                                                  return_type,
                                                  parameter_types,
                                                  parameter_count,
                                                  is_variadic,
                                                  is_internal,
                                                  assembler_name,
                                                  assembler_name_length,
                                                  has_assembler_name,
                                                  visibility,
                                                  has_visibility,
                                                  section_name,
                                                  section_name_length,
                                                  has_section);
    }
'''
replace_once("src/frontend/parser_function.c", old, new)

# Focused semantic coverage: function typedef and typeof(function type) must
# produce Function entities/direct calls; pointer-to-function typedef remains an
# object and an indirect call.
(ROOT / "tests/compiler/c0/function_typed_declarator.c").write_text(
    '''struct perf_branch_entry {\n    unsigned long from;\n};\n\ntypedef int perf_snapshot_branch_stack_t(struct perf_branch_entry *entries, unsigned int cnt);\ntypedef int (*perf_snapshot_branch_stack_ptr_t)(struct perf_branch_entry *entries, unsigned int cnt);\n\nextern typeof(perf_snapshot_branch_stack_t) __SCT__perf_snapshot_branch_stack;\nextern perf_snapshot_branch_stack_t typed_direct;\nextern int typed_direct(struct perf_branch_entry *entries, unsigned int cnt);\nextern perf_snapshot_branch_stack_t (parenthesized_direct);\nextern perf_snapshot_branch_stack_ptr_t callback_slot;\n\nint invoke_typed(struct perf_branch_entry *entries) {\n    return __SCT__perf_snapshot_branch_stack(entries, 1U) + typed_direct(entries, 2U) +\n           parenthesized_direct(entries, 3U) + callback_slot(entries, 4U);\n}\n'''
)
(ROOT / "tests/compiler/c0/invalid_function_typed_redeclaration.c").write_text(
    '''typedef int callback_t(int value);\nextern callback_t callback;\nextern int callback(long value);\n\nint main(void) {\n    return 0;\n}\n'''
)
(ROOT / "tests/compiler/c0/invalid_function_typed_definition.c").write_text(
    '''typedef int callback_t(int value);\ncallback_t callback {\n    return 1;\n}\n'''
)
(ROOT / "tests/compiler/c0/run-function-typed-declarator.sh").write_text(
    r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug/function-typed-declarator"}
mkdir -p "$work"

preprocess() {
    name=$1
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
}

preprocess function_typed_declarator
"$minic" -S "$work/function_typed_declarator.i" -o "$work/function_typed_declarator.s"
grep -F "  call __SCT__perf_snapshot_branch_stack" "$work/function_typed_declarator.s" >/dev/null
grep -F "  call typed_direct" "$work/function_typed_declarator.s" >/dev/null
grep -F "  call parenthesized_direct" "$work/function_typed_declarator.s" >/dev/null
grep -F "  la a0, callback_slot" "$work/function_typed_declarator.s" >/dev/null
grep -F "  jalr ra, t0, 0" "$work/function_typed_declarator.s" >/dev/null

preprocess invalid_function_typed_redeclaration
if "$minic" -S "$work/invalid_function_typed_redeclaration.i" \
    -o "$work/invalid_function_typed_redeclaration.s" \
    >"$work/invalid-redecl.stdout" 2>"$work/invalid-redecl.stderr"; then
    echo "FAIL compiler/c0/invalid_function_typed_redeclaration: compilation unexpectedly succeeded" >&2
    exit 1
fi
grep -F "conflicting function declaration" "$work/invalid-redecl.stderr" >/dev/null

preprocess invalid_function_typed_definition
if "$minic" -S "$work/invalid_function_typed_definition.i" \
    -o "$work/invalid_function_typed_definition.s" \
    >"$work/invalid-definition.stdout" 2>"$work/invalid-definition.stderr"; then
    echo "FAIL compiler/c0/invalid_function_typed_definition: compilation unexpectedly succeeded" >&2
    exit 1
fi

printf '%s\n' "PASS compiler/c0/function_typed_declarator entity=function typedef=direct typeof=function parenthesized=1 redeclaration=shared-signature pointer-typedef=object+jalr definition=reject"
'''
)

run = ROOT / "tests/compiler/c0/run.sh"
text = run.read_text()
line = 'MINIC="$minic" BUILD_DIR="$work/function-typed-declarator" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-function-typed-declarator.sh"\n'
if line not in text:
    if not text.endswith("\n"):
        text += "\n"
    text += "\n" + line
    run.write_text(text)
