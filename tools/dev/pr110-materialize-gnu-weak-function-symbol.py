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


# Attribute registry: weak is symbol binding, not parse-only metadata.
replace_once(
    "src/frontend/attribute.h",
    """    MINIC_ATTRIBUTE_GNU_INLINE,\n    MINIC_ATTRIBUTE_SECTION,\n""",
    """    MINIC_ATTRIBUTE_GNU_INLINE,\n    MINIC_ATTRIBUTE_WEAK,\n    MINIC_ATTRIBUTE_SECTION,\n""",
)
replace_once(
    "src/frontend/attribute.c",
    """    MINIC_ATTRIBUTE_ENTRY(\"__gnu_inline__\",\n                          MINIC_ATTRIBUTE_GNU_INLINE,\n                          MINIC_ATTRIBUTE_CLASS_LANGUAGE_SEMANTIC,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION),\n    MINIC_ATTRIBUTE_ENTRY(\"section\",\n""",
    """    MINIC_ATTRIBUTE_ENTRY(\"__gnu_inline__\",\n                          MINIC_ATTRIBUTE_GNU_INLINE,\n                          MINIC_ATTRIBUTE_CLASS_LANGUAGE_SEMANTIC,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION),\n    MINIC_ATTRIBUTE_ENTRY(\"weak\",\n                          MINIC_ATTRIBUTE_WEAK,\n                          MINIC_ATTRIBUTE_CLASS_SYMBOL,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION),\n    MINIC_ATTRIBUTE_ENTRY(\"__weak__\",\n                          MINIC_ATTRIBUTE_WEAK,\n                          MINIC_ATTRIBUTE_CLASS_SYMBOL,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION),\n    MINIC_ATTRIBUTE_ENTRY(\"section\",\n""",
)

# Function entity owns weak binding persistently.
replace_once(
    "src/frontend/ast.h",
    """    bool is_defined;\n    bool is_internal;\n    bool is_variadic;\n} MinicFunction;\n""",
    """    bool is_defined;\n    bool is_internal;\n    bool is_variadic;\n    bool is_weak;\n} MinicFunction;\n""",
)
replace_once(
    "src/frontend/ast.h",
    """bool minic_c0_program_set_function_internal(MinicC0Program *program,\n                                            MinicFunctionId function_id,\n                                            bool is_internal);\nbool minic_c0_program_set_function_assembler_name(MinicC0Program *program,\n""",
    """bool minic_c0_program_set_function_internal(MinicC0Program *program,\n                                            MinicFunctionId function_id,\n                                            bool is_internal);\nbool minic_c0_program_set_function_weak(MinicC0Program *program,\n                                        MinicFunctionId function_id,\n                                        bool is_weak);\nbool minic_c0_program_set_function_assembler_name(MinicC0Program *program,\n""",
)
replace_once(
    "src/frontend/ast_function.c",
    """bool minic_c0_program_set_function_internal(MinicC0Program *program,\n                                            MinicFunctionId function_id,\n                                            bool is_internal) {\n    if (program == NULL || function_id >= program->function_count) {\n        return false;\n    }\n    program->functions[function_id].is_internal = is_internal;\n    return true;\n}\n\n""",
    """bool minic_c0_program_set_function_internal(MinicC0Program *program,\n                                            MinicFunctionId function_id,\n                                            bool is_internal) {\n    if (program == NULL || function_id >= program->function_count ||\n        (is_internal && program->functions[function_id].is_weak)) {\n        return false;\n    }\n    program->functions[function_id].is_internal = is_internal;\n    return true;\n}\n\nbool minic_c0_program_set_function_weak(MinicC0Program *program,\n                                        MinicFunctionId function_id,\n                                        bool is_weak) {\n    if (program == NULL || function_id >= program->function_count ||\n        (is_weak && program->functions[function_id].is_internal)) {\n        return false;\n    }\n    program->functions[function_id].is_weak = is_weak;\n    return true;\n}\n\n""",
)
replace_once(
    "src/frontend/ast_verifier.c",
    """            function->local_count > program->local_count - function->local_begin ||\n            (function->is_defined && function->body_block >= program->block_count) ||\n""",
    """            function->local_count > program->local_count - function->local_begin ||\n            (function->is_internal && function->is_weak) ||\n            (function->is_defined && function->body_block >= program->block_count) ||\n""",
)

# Function attribute consumer persists weak metadata at both prefix/deferred and suffix placements.
replace_once(
    "src/frontend/parser_function.c",
    """    size_t *section_name_length;\n    bool *has_section;\n    const char *unsupported_message;\n} MinicFunctionAttributeContext;\n""",
    """    size_t *section_name_length;\n    bool *has_section;\n    bool *is_weak;\n    const char *unsupported_message;\n} MinicFunctionAttributeContext;\n""",
)
replace_once(
    "src/frontend/parser_function.c",
    """    const MinicFunctionAttributeContext *context;\n    const MinicAttributeDescriptor *descriptor;\n\n    if (parser == NULL || attribute == NULL || opaque_context == NULL) {\n        return false;\n    }\n    context = (const MinicFunctionAttributeContext *)opaque_context;\n""",
    """    MinicFunctionAttributeContext *context;\n    const MinicAttributeDescriptor *descriptor;\n\n    if (parser == NULL || attribute == NULL || opaque_context == NULL) {\n        return false;\n    }\n    context = (MinicFunctionAttributeContext *)opaque_context;\n""",
)
replace_once(
    "src/frontend/parser_function.c",
    """        return true;\n    }\n\n    if (descriptor->kind == MINIC_ATTRIBUTE_GNU_INLINE) {\n""",
    """        return true;\n    }\n\n    if (descriptor->kind == MINIC_ATTRIBUTE_WEAK) {\n        if (context->is_weak == NULL || context->is_internal) {\n            minic_parser_error(parser, \"GNU weak requires external function linkage\");\n            return false;\n        }\n        *context->is_weak = true;\n        return true;\n    }\n\n    if (descriptor->kind == MINIC_ATTRIBUTE_GNU_INLINE) {\n""",
)
replace_once(
    "src/frontend/parser_function.c",
    """static bool parse_function_attribute_lists(MinicParser *parser,\n                                           bool allow_gnu_inline,\n                                           bool is_internal,\n                                           bool is_inline,\n                                           const char *unsupported_message) {\n""",
    """static bool parse_function_attribute_lists(MinicParser *parser,\n                                           bool allow_gnu_inline,\n                                           bool is_internal,\n                                           bool is_inline,\n                                           bool *is_weak,\n                                           const char *unsupported_message) {\n""",
)
replace_once(
    "src/frontend/parser_function.c",
    """    context.section_name_length = NULL;\n    context.has_section = NULL;\n    context.unsupported_message = unsupported_message;\n""",
    """    context.section_name_length = NULL;\n    context.has_section = NULL;\n    context.is_weak = is_weak;\n    context.unsupported_message = unsupported_message;\n""",
)
replace_once(
    "src/frontend/parser_function.c",
    """                                          size_t *section_name_length,\n                                          bool *has_section,\n                                          const char *unsupported_message) {\n""",
    """                                          size_t *section_name_length,\n                                          bool *has_section,\n                                          bool *is_weak,\n                                          const char *unsupported_message) {\n""",
)
replace_once(
    "src/frontend/parser_function.c",
    """    context.section_name_length = section_name_length;\n    context.has_section = has_section;\n    context.unsupported_message = unsupported_message;\n""",
    """    context.section_name_length = section_name_length;\n    context.has_section = has_section;\n    context.is_weak = is_weak;\n    context.unsupported_message = unsupported_message;\n""",
)
# Public parse-only wrappers still reject symbol-affecting weak because they have no entity state.
replace_once(
    "src/frontend/parser_function.c",
    """        false,\n        false,\n        false,\n        \"unsupported GNU function attribute; ABI/layout-affecting and unknown attributes must be \"\n""",
    """        false,\n        false,\n        false,\n        NULL,\n        \"unsupported GNU function attribute; ABI/layout-affecting and unknown attributes must be \"\n""",
)
replace_once(
    "src/frontend/parser_function.c",
    """        true,\n        is_internal,\n        is_inline,\n        \"unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must be \"\n""",
    """        true,\n        is_internal,\n        is_inline,\n        NULL,\n        \"unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must be \"\n""",
)
replace_once(
    "src/frontend/parser_function.c",
    """bool minic_parser_parse_gnu_prefix_function_attributes(MinicParser *parser,\n                                                       bool is_internal,\n                                                       bool is_inline) {\n    return parse_function_attribute_lists(\n        parser,\n        true,\n        is_internal,\n        is_inline,\n        NULL,\n        \"unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must be \"\n        \"implemented explicitly\");\n}\n\n""",
    """bool minic_parser_parse_gnu_prefix_function_attributes(MinicParser *parser,\n                                                       bool is_internal,\n                                                       bool is_inline) {\n    return parse_function_attribute_lists(\n        parser,\n        true,\n        is_internal,\n        is_inline,\n        NULL,\n        \"unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must be \"\n        \"implemented explicitly\");\n}\n\nstatic bool parse_persistent_function_attributes(MinicParser *parser,\n                                                 bool is_internal,\n                                                 bool *is_weak) {\n    return parse_function_attribute_lists(\n        parser,\n        false,\n        is_internal,\n        false,\n        is_weak,\n        \"unsupported GNU function attribute; ABI/layout-affecting and unknown attributes must be \"\n        \"implemented explicitly\");\n}\n\n""",
)

# Declaration helper persists weak on the shared Function entity.
replace_once(
    "src/frontend/parser_function.c",
    """                                               size_t parameter_count,\n                                               bool is_variadic,\n                                               bool is_internal,\n                                               const char *assembler_name,\n""",
    """                                               size_t parameter_count,\n                                               bool is_variadic,\n                                               bool is_internal,\n                                               bool is_weak,\n                                               const char *assembler_name,\n""",
)
replace_once(
    "src/frontend/parser_function.c",
    """    if (parser == NULL || parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||\n        (parameter_count != 0U && parameter_types == NULL) ||\n        parser->current.kind != MINIC_TOKEN_SEMICOLON) {\n        return false;\n    }\n""",
    """    if (parser == NULL || parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||\n        (parameter_count != 0U && parameter_types == NULL) ||\n        parser->current.kind != MINIC_TOKEN_SEMICOLON) {\n        return false;\n    }\n    if (is_weak && is_internal) {\n        minic_parser_error(parser, \"GNU weak requires external function linkage\");\n        return false;\n    }\n""",
)
replace_once(
    "src/frontend/parser_function.c",
    """    if (has_assembler_name &&\n        !minic_c0_program_set_function_assembler_name(\n            parser->program, function_id, assembler_name, assembler_name_length)) {\n""",
    """    if (is_weak && !minic_c0_program_set_function_weak(parser->program, function_id, true)) {\n        minic_parser_error(parser, \"conflicting GNU weak function linkage\");\n        return false;\n    }\n    if (has_assembler_name &&\n        !minic_c0_program_set_function_assembler_name(\n            parser->program, function_id, assembler_name, assembler_name_length)) {\n""",
)

# Parse-function pending state.
replace_once(
    "src/frontend/parser_function.c",
    """    bool is_main;\n    bool is_variadic;\n    char assembler_name[256];\n""",
    """    bool is_main;\n    bool is_variadic;\n    bool is_weak;\n    char assembler_name[256];\n""",
)
replace_once(
    "src/frontend/parser_function.c",
    """    is_variadic = false;\n    assembler_name_length = 0U;\n""",
    """    is_variadic = false;\n    is_weak = false;\n    assembler_name_length = 0U;\n""",
)
# Both deferred/prefix function attribute applications persist weak.
old_apply_tail = """                &section_name_length,\n                &has_section,\n                \"unsupported GNU prefix function attribute; semantic and ABI-affecting attributes \"\n"""
new_apply_tail = """                &section_name_length,\n                &has_section,\n                &is_weak,\n                \"unsupported GNU prefix function attribute; semantic and ABI-affecting attributes \"\n"""
path = ROOT / "src/frontend/parser_function.c"
text = path.read_text()
count = text.count(old_apply_tail)
if count != 2:
    raise SystemExit(f"parser_function.c: expected two persistent prefix attribute call sites, found {count}")
path.write_text(text.replace(old_apply_tail, new_apply_tail))

# Both top-level suffix sites must persist symbol metadata.
path = ROOT / "src/frontend/parser_function.c"
text = path.read_text()
old_suffix = "!minic_parser_parse_gnu_function_attributes(parser)"
count = text.count(old_suffix)
if count != 2:
    raise SystemExit(f"parser_function.c: expected two top-level suffix attribute calls, found {count}")
path.write_text(text.replace(old_suffix, "!parse_persistent_function_attributes(parser, is_internal, &is_weak)"))

# Both declaration helper call sites receive weak state.
path = ROOT / "src/frontend/parser_function.c"
text = path.read_text()
old_call = """                                                  is_variadic,\n                                                  is_internal,\n                                                  assembler_name,\n"""
if text.count(old_call) != 1:
    raise SystemExit("parser_function.c: ordinary declaration helper call anchor mismatch")
text = text.replace(
    old_call,
    """                                                  is_variadic,\n                                                  is_internal,\n                                                  is_weak,\n                                                  assembler_name,\n""",
    1,
)
old_typed_call = """                                                  false,\n                                                  is_internal,\n                                                  assembler_name,\n"""
if text.count(old_typed_call) != 1:
    raise SystemExit("parser_function.c: function-typed helper call anchor mismatch")
text = text.replace(
    old_typed_call,
    """                                                  false,\n                                                  is_internal,\n                                                  is_weak,\n                                                  assembler_name,\n""",
    1,
)
path.write_text(text)

# Function definitions persist weak after entity creation/reuse.
replace_once(
    "src/frontend/parser_function.c",
    """    if (has_assembler_name &&\n        !minic_c0_program_set_function_assembler_name(\n            parser->program, function_id, assembler_name, assembler_name_length)) {\n        minic_parser_error(parser, \"conflicting or invalid GNU function asm label\");\n""",
    """    if (is_weak && !minic_c0_program_set_function_weak(parser->program, function_id, true)) {\n        minic_parser_error(parser, \"conflicting GNU weak function linkage\");\n        return false;\n    }\n    if (has_assembler_name &&\n        !minic_c0_program_set_function_assembler_name(\n            parser->program, function_id, assembler_name, assembler_name_length)) {\n        minic_parser_error(parser, \"conflicting or invalid GNU function asm label\");\n""",
)

# RV64 symbol binding: weak definitions replace .globl; declaration-only weak
# entities still emit a linker-visible .weak directive without a body.
replace_once(
    "src/target/riscv64/codegen_function.c",
    """    if (success && !function->is_internal) {\n        success = fprintf(file, \".globl %s\\n\", symbol_name) >= 0;\n        if (success && function->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT) {\n""",
    """    if (success && !function->is_internal) {\n        success = fprintf(file, function->is_weak ? \".weak %s\\n\" : \".globl %s\\n\",\n                          symbol_name) >= 0;\n        if (success && function->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT) {\n""",
)
replace_once(
    "src/target/riscv64/codegen_function.c",
    """        function = &program->functions[function_index];\n        if (!function->is_defined) {\n            continue;\n        }\n        success = minic_riscv64_emit_function(file, program, function, &label_counter);\n""",
    """        function = &program->functions[function_index];\n        if (!function->is_defined) {\n            const char *symbol_name;\n\n            symbol_name = minic_c0_function_symbol_name(function);\n            if (function->is_weak && !function->is_internal) {\n                success = symbol_name != NULL && symbol_name[0] != '\\0' &&\n                          fprintf(file, \".weak %s\\n\", symbol_name) >= 0;\n            }\n            continue;\n        }\n        success = minic_riscv64_emit_function(file, program, function, &label_counter);\n""",
)

# Focused semantic boundary.
(ROOT / "tests/compiler/c0/gnu_weak_function_symbol.c").write_text(
    """void __attribute__((weak)) calibration_delay_done(void);\nvoid optional_hook(void) __attribute__((__weak__));\nvoid later_weak(void);\nvoid __attribute__((weak)) later_weak(void);\n\nint __attribute__((weak)) weak_definition(void) {\n    return 7;\n}\n\nint strong_definition(void) {\n    return 9;\n}\n\nvoid invoke_hooks(void) {\n    calibration_delay_done();\n    optional_hook();\n    later_weak();\n}\n"""
)
(ROOT / "tests/compiler/c0/invalid_static_weak_function.c").write_text(
    """static void __attribute__((weak)) hidden_hook(void);\n\nint main(void) {\n    return 0;\n}\n"""
)
(ROOT / "tests/compiler/c0/run-gnu-weak-function-symbol.sh").write_text(
    r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug/gnu-weak-function-symbol"}
mkdir -p "$work"

preprocess() {
    name=$1
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
}

preprocess gnu_weak_function_symbol
"$minic" -S "$work/gnu_weak_function_symbol.i" -o "$work/gnu_weak_function_symbol.s"
for symbol in calibration_delay_done optional_hook later_weak weak_definition; do
    grep -F ".weak $symbol" "$work/gnu_weak_function_symbol.s" >/dev/null
done
grep -F ".globl strong_definition" "$work/gnu_weak_function_symbol.s" >/dev/null
if grep -F ".globl weak_definition" "$work/gnu_weak_function_symbol.s" >/dev/null; then
    echo "FAIL compiler/c0/gnu_weak_function_symbol: weak definition also emitted .globl" >&2
    exit 1
fi
for symbol in calibration_delay_done optional_hook later_weak; do
    if grep -F "$symbol:" "$work/gnu_weak_function_symbol.s" >/dev/null; then
        echo "FAIL compiler/c0/gnu_weak_function_symbol: declaration-only weak function emitted a body" >&2
        exit 1
    fi
done
grep -F "weak_definition:" "$work/gnu_weak_function_symbol.s" >/dev/null
grep -F "  call calibration_delay_done" "$work/gnu_weak_function_symbol.s" >/dev/null
grep -F "  call optional_hook" "$work/gnu_weak_function_symbol.s" >/dev/null
grep -F "  call later_weak" "$work/gnu_weak_function_symbol.s" >/dev/null

preprocess invalid_static_weak_function
if "$minic" -S "$work/invalid_static_weak_function.i" -o "$work/invalid_static_weak_function.s" \
    >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    echo "FAIL compiler/c0/invalid_static_weak_function: compilation unexpectedly succeeded" >&2
    exit 1
fi
grep -F "GNU weak requires external function linkage" "$work/invalid.stderr" >/dev/null

printf '%s\n' "PASS compiler/c0/gnu_weak_function_symbol registry=symbol binding=weak prefix+suffix=1 redeclaration=inherited declaration-only=.weak definition=.weak-not-globl strong=.globl static=reject"
'''
)

run = ROOT / "tests/compiler/c0/run.sh"
text = run.read_text()
line = 'MINIC="$minic" BUILD_DIR="$work/gnu-weak-function-symbol" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-gnu-weak-function-symbol.sh"\n'
if line not in text:
    if not text.endswith("\n"):
        text += "\n"
    text += "\n" + line
    run.write_text(text)
