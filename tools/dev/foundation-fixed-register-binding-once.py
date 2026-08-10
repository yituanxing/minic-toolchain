#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


# AST ownership and expression identity.
path = Path("src/frontend/ast.h")
text = path.read_text()
text = replace_once(
    text,
    "typedef size_t MinicGlobalObjectId;\ntypedef size_t MinicInlineAsmId;\n",
    "typedef size_t MinicGlobalObjectId;\ntypedef size_t MinicFixedRegisterBindingId;\ntypedef size_t MinicInlineAsmId;\n",
    "fixed-register-id",
)
text = replace_once(
    text,
    "#define MINIC_GLOBAL_OBJECT_INVALID ((MinicGlobalObjectId) - 1)\n#define MINIC_INLINE_ASM_INVALID",
    "#define MINIC_GLOBAL_OBJECT_INVALID ((MinicGlobalObjectId) - 1)\n#define MINIC_FIXED_REGISTER_BINDING_INVALID ((MinicFixedRegisterBindingId) - 1)\n#define MINIC_INLINE_ASM_INVALID",
    "fixed-register-invalid",
)
text = replace_once(
    text,
    "    MINIC_EXPRESSION_GLOBAL_OBJECT,\n    MINIC_EXPRESSION_FUNCTION,\n",
    "    MINIC_EXPRESSION_GLOBAL_OBJECT,\n    MINIC_EXPRESSION_FIXED_REGISTER,\n    MINIC_EXPRESSION_FUNCTION,\n",
    "fixed-register-expression-kind",
)
text = replace_once(
    text,
    "        MinicGlobalObjectId global_object_id;\n        MinicFunctionId function_id;\n",
    "        MinicGlobalObjectId global_object_id;\n        MinicFixedRegisterBindingId fixed_register_binding_id;\n        MinicFunctionId function_id;\n",
    "fixed-register-expression-value",
)
text = replace_once(
    text,
    "typedef struct MinicGlobalFunctionRelocation {\n",
    "typedef struct MinicFixedRegisterBinding {\n"
    "    char *name;\n"
    "    size_t name_length;\n"
    "    char *register_name;\n"
    "    size_t register_name_length;\n"
    "    MinicType type;\n"
    "} MinicFixedRegisterBinding;\n\n"
    "typedef struct MinicGlobalFunctionRelocation {\n",
    "fixed-register-struct",
)
text = replace_once(
    text,
    "    MinicGlobalObject *global_objects;\n    size_t global_object_count;\n    size_t global_object_capacity;\n\n    MinicExpressionId return_expression;\n",
    "    MinicGlobalObject *global_objects;\n"
    "    size_t global_object_count;\n"
    "    size_t global_object_capacity;\n\n"
    "    MinicFixedRegisterBinding *fixed_register_bindings;\n"
    "    size_t fixed_register_binding_count;\n"
    "    size_t fixed_register_binding_capacity;\n\n"
    "    MinicExpressionId return_expression;\n",
    "fixed-register-program-storage",
)
text = replace_once(
    text,
    "bool minic_c0_program_add_global_object(MinicC0Program *program,\n",
    "bool minic_c0_program_add_fixed_register_binding(MinicC0Program *program,\n"
    "                                                 const char *name,\n"
    "                                                 size_t name_length,\n"
    "                                                 MinicType type,\n"
    "                                                 const char *register_name,\n"
    "                                                 size_t register_name_length,\n"
    "                                                 MinicFixedRegisterBindingId *binding_id);\n"
    "bool minic_c0_program_add_global_object(MinicC0Program *program,\n",
    "fixed-register-add-prototype",
)
text = replace_once(
    text,
    "const MinicGlobalObject *minic_c0_program_global_object(const MinicC0Program *program,\n"
    "                                                        MinicGlobalObjectId global_object_id);\n",
    "const MinicGlobalObject *minic_c0_program_global_object(const MinicC0Program *program,\n"
    "                                                        MinicGlobalObjectId global_object_id);\n"
    "const MinicFixedRegisterBinding *minic_c0_program_fixed_register_binding(\n"
    "    const MinicC0Program *program, MinicFixedRegisterBindingId binding_id);\n",
    "fixed-register-get-prototype",
)
path.write_text(text)

# Program lifetime owns fixed binding names.
path = Path("src/frontend/ast.c")
text = path.read_text()
text = replace_once(
    text,
    "    for (index = 0U; index < program->global_object_count; ++index) {\n"
    "        free(program->global_objects[index].name);\n",
    "    for (index = 0U; index < program->fixed_register_binding_count; ++index) {\n"
    "        free(program->fixed_register_bindings[index].name);\n"
    "        free(program->fixed_register_bindings[index].register_name);\n"
    "    }\n"
    "    for (index = 0U; index < program->global_object_count; ++index) {\n"
    "        free(program->global_objects[index].name);\n",
    "fixed-register-destroy-loop",
)
text = replace_once(
    text,
    "    free(program->type_aliases);\n    free(program->global_objects);\n",
    "    free(program->type_aliases);\n    free(program->global_objects);\n    free(program->fixed_register_bindings);\n",
    "fixed-register-destroy-storage",
)
path.write_text(text)

# Program-level fixed binding table stays separate from memory global objects.
path = Path("src/frontend/ast_global.c")
text = path.read_text()
text = replace_once(
    text,
    "    for (index = 0U; index < program->global_object_count; ++index) {\n",
    "    for (index = 0U; index < program->fixed_register_binding_count; ++index) {\n"
    "        const MinicFixedRegisterBinding *binding;\n\n"
    "        binding = &program->fixed_register_bindings[index];\n"
    "        if (binding->name_length == name_length &&\n"
    "            memcmp(binding->name, name, name_length) == 0) {\n"
    "            return true;\n"
    "        }\n"
    "    }\n"
    "    for (index = 0U; index < program->global_object_count; ++index) {\n",
    "fixed-register-name-conflict",
)
insert = r'''bool minic_c0_program_add_fixed_register_binding(MinicC0Program *program,
                                                 const char *name,
                                                 size_t name_length,
                                                 MinicType type,
                                                 const char *register_name,
                                                 size_t register_name_length,
                                                 MinicFixedRegisterBindingId *binding_id) {
    MinicFixedRegisterBinding binding;

    if (program == NULL || name == NULL || register_name == NULL || binding_id == NULL ||
        register_name_length == 0U ||
        (!minic_type_is_integer(type) && !minic_type_is_pointer(type)) ||
        name_conflicts(program, name, name_length) ||
        !grow_array((void **)&program->fixed_register_bindings,
                    &program->fixed_register_binding_capacity,
                    program->fixed_register_binding_count,
                    sizeof(*program->fixed_register_bindings))) {
        return false;
    }
    (void)memset(&binding, 0, sizeof(binding));
    binding.name = copy_name(name, name_length);
    binding.register_name = copy_name(register_name, register_name_length);
    if (binding.name == NULL || binding.register_name == NULL) {
        free(binding.name);
        free(binding.register_name);
        return false;
    }
    binding.name_length = name_length;
    binding.register_name_length = register_name_length;
    binding.type = type;
    *binding_id = program->fixed_register_binding_count;
    program->fixed_register_bindings[program->fixed_register_binding_count] = binding;
    program->fixed_register_binding_count += 1U;
    return true;
}

'''
text = replace_once(
    text,
    "bool minic_c0_program_add_global_object(MinicC0Program *program,\n",
    insert + "bool minic_c0_program_add_global_object(MinicC0Program *program,\n",
    "fixed-register-add-implementation",
)
getter = r'''const MinicFixedRegisterBinding *minic_c0_program_fixed_register_binding(
    const MinicC0Program *program, MinicFixedRegisterBindingId binding_id) {
    if (program == NULL || binding_id >= program->fixed_register_binding_count) {
        return NULL;
    }
    return &program->fixed_register_bindings[binding_id];
}

'''
text = replace_once(
    text,
    "bool minic_c0_global_object_set_visibility(MinicC0Program *program,\n",
    getter + "bool minic_c0_global_object_set_visibility(MinicC0Program *program,\n",
    "fixed-register-get-implementation",
)
path.write_text(text)

# Parser lookup seam.
path = Path("src/frontend/parser_internal.h")
text = path.read_text()
text = replace_once(
    text,
    "MinicGlobalObjectId minic_parser_find_global_object(const MinicParser *parser,\n"
    "                                                    MinicSourceSpan name_span);\n",
    "MinicGlobalObjectId minic_parser_find_global_object(const MinicParser *parser,\n"
    "                                                    MinicSourceSpan name_span);\n"
    "MinicFixedRegisterBindingId minic_parser_find_fixed_register_binding(\n"
    "    const MinicParser *parser, MinicSourceSpan name_span);\n",
    "fixed-register-parser-prototype",
)
path.write_text(text)

path = Path("src/frontend/parser_global.c")
text = path.read_text()
lookup = r'''MinicFixedRegisterBindingId minic_parser_find_fixed_register_binding(
    const MinicParser *parser, MinicSourceSpan name_span) {
    size_t name_length;
    size_t index;

    if (parser == NULL || parser->program == NULL) {
        return MINIC_FIXED_REGISTER_BINDING_INVALID;
    }
    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->fixed_register_binding_count; ++index) {
        const MinicFixedRegisterBinding *binding;

        binding = minic_c0_program_fixed_register_binding(parser->program, index);
        if (binding != NULL && binding->name_length == name_length &&
            memcmp(binding->name, parser->source + name_span.begin.offset, name_length) == 0) {
            return index;
        }
    }
    return MINIC_FIXED_REGISTER_BINDING_INVALID;
}

'''
text = replace_once(
    text,
    "static bool token_starts_type_name(MinicTokenKind kind) {\n",
    lookup + "static bool token_starts_type_name(MinicTokenKind kind) {\n",
    "fixed-register-parser-lookup",
)
path.write_text(text)

# TargetInfo owns which fixed registers this backend can safely bind.
path = Path("src/target/target_info.h")
text = path.read_text()
text = replace_once(
    text,
    "bool minic_target_info_integer_width(const MinicTargetInfo *target,\n"
    "                                     const MinicC0Program *program,\n"
    "                                     MinicType type,\n"
    "                                     unsigned int *bits);\n",
    "bool minic_target_info_integer_width(const MinicTargetInfo *target,\n"
    "                                     const MinicC0Program *program,\n"
    "                                     MinicType type,\n"
    "                                     unsigned int *bits);\n"
    "bool minic_target_info_fixed_register_supported(const MinicTargetInfo *target,\n"
    "                                                const char *name,\n"
    "                                                size_t name_length);\n",
    "fixed-register-target-prototype",
)
path.write_text(text)

path = Path("src/target/target_info.c")
text = path.read_text()
text = replace_once(text, "#include <limits.h>\n", "#include <limits.h>\n#include <string.h>\n", "target-string-include")
text += r'''

bool minic_target_info_fixed_register_supported(const MinicTargetInfo *target,
                                                const char *name,
                                                size_t name_length) {
    if (target == NULL || name == NULL) {
        return false;
    }
    /* The current RV64 backend uses most caller/scratch registers internally.
     * Only the architectural stack/thread registers observed in unchanged Linux
     * are safe fixed bindings until register allocation becomes target-aware. */
    return (name_length == 2U && memcmp(name, "tp", 2U) == 0) ||
           (name_length == 2U && memcmp(name, "sp", 2U) == 0);
}
'''
path.write_text(text)

# Top-level register declarations become fixed bindings, not memory global objects.
path = Path("src/frontend/parser_function.c")
text = path.read_text()
text = replace_once(
    text,
    "    bool is_extern;\n    bool is_static;\n    bool is_inline;\n",
    "    bool is_extern;\n    bool is_static;\n    bool is_register;\n    bool is_inline;\n",
    "register-prefix-state",
)
register_prefix = r'''        if (function_identifier_is(parser, "register")) {
            if (saw_storage_class) {
                minic_parser_error(parser, "conflicting or duplicate declaration storage class");
                return false;
            }
            prefix->is_register = true;
            saw_storage_class = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
'''
text = replace_once(
    text,
    "        if (parser->current.kind == MINIC_TOKEN_KW_STATIC) {\n",
    register_prefix + "        if (parser->current.kind == MINIC_TOKEN_KW_STATIC) {\n",
    "register-prefix-parser",
)
text = replace_once(
    text,
    "    bool is_function_pointer_object;\n    bool is_inline;\n    bool is_static_declaration;\n",
    "    bool is_function_pointer_object;\n    bool is_inline;\n    bool is_register_declaration;\n    bool is_static_declaration;\n",
    "register-function-state",
)
text = replace_once(
    text,
    "    is_function_pointer_object = false;\n    is_inline = false;\n    is_static_declaration = false;\n",
    "    is_function_pointer_object = false;\n    is_inline = false;\n    is_register_declaration = false;\n    is_static_declaration = false;\n",
    "register-function-init",
)
text = replace_once(
    text,
    "    is_extern_declaration = declaration_prefix.is_extern;\n    is_static_declaration = declaration_prefix.is_static;\n",
    "    is_extern_declaration = declaration_prefix.is_extern;\n"
    "    is_static_declaration = declaration_prefix.is_static;\n"
    "    is_register_declaration = declaration_prefix.is_register;\n",
    "register-function-prefix-copy",
)
fixed_parser = r'''    if (is_register_declaration) {
        MinicFixedRegisterBindingId binding_id;

        if (is_inline || deferred_attributes.count != 0U || has_section || has_visibility ||
            is_function_pointer_object || parser->current.kind == MINIC_TOKEN_LPAREN ||
            (!minic_type_is_integer(return_type) && !minic_type_is_pointer(return_type))) {
            minic_parser_error(parser, "unsupported file-scope register declaration shape");
            return false;
        }
        if (!parse_gnu_function_asm_label(parser,
                                          assembler_name,
                                          sizeof(assembler_name),
                                          &assembler_name_length,
                                          &has_assembler_name) ||
            !has_assembler_name) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "file-scope register declaration requires GNU asm name");
            }
            return false;
        }
        if (!minic_target_info_fixed_register_supported(
                parser->target_info, assembler_name, assembler_name_length)) {
            minic_parser_error(parser, "fixed register binding is not supported by this target");
            return false;
        }
        if (!minic_c0_program_add_fixed_register_binding(
                parser->program,
                parser->source + name_span.begin.offset,
                minic_parser_span_length(name_span),
                return_type,
                assembler_name,
                assembler_name_length,
                &binding_id)) {
            minic_parser_error(parser, "cannot record fixed register binding");
            return false;
        }
        (void)binding_id;
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after fixed register binding");
    }
'''
text = replace_once(
    text,
    "    if (is_static_declaration && parser->current.kind != MINIC_TOKEN_LPAREN) {\n",
    fixed_parser + "    if (is_static_declaration && parser->current.kind != MINIC_TOKEN_LPAREN) {\n",
    "register-declaration-dispatch",
)
path.write_text(text)

# Name references produce a read-only semantic expression, never a fake memory lvalue.
path = Path("src/frontend/parser_expression.c")
text = path.read_text()
helper = r'''static bool parse_fixed_register_reference(MinicParser *parser,
                                           MinicSourceSpan name_span,
                                           MinicFixedRegisterBindingId binding_id,
                                           MinicExpressionId *expression_id) {
    const MinicFixedRegisterBinding *binding;
    MinicExpression expression;
    MinicExpressionId base_id;

    binding = minic_c0_program_fixed_register_binding(parser->program, binding_id);
    if (binding == NULL) {
        minic_parser_error(parser, "invalid fixed register reference");
        return false;
    }
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_FIXED_REGISTER;
    expression.span = name_span;
    expression.type = binding->type;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.fixed_register_binding_id = binding_id;
    if (!minic_parser_add_expression(parser, &expression, &base_id)) {
        return false;
    }
    return minic_parser_parse_postfix(parser, base_id, expression_id);
}

'''
text = replace_once(
    text,
    "static bool parse_function_reference(MinicParser *parser,\n",
    helper + "static bool parse_function_reference(MinicParser *parser,\n",
    "fixed-register-reference-helper",
)
text = replace_once(
    text,
    "    MinicGlobalObjectId global_object_id;\n    int enum_value;\n",
    "    MinicGlobalObjectId global_object_id;\n    MinicFixedRegisterBindingId fixed_register_binding_id;\n    int enum_value;\n",
    "fixed-register-primary-state",
)
text = replace_once(
    text,
    "        global_object_id = minic_parser_find_global_object(parser, name_span);\n"
    "        is_enum_constant = minic_parser_find_enum_constant(parser, name_span, &enum_value);\n",
    "        global_object_id = minic_parser_find_global_object(parser, name_span);\n"
    "        fixed_register_binding_id = minic_parser_find_fixed_register_binding(parser, name_span);\n"
    "        is_enum_constant = minic_parser_find_enum_constant(parser, name_span, &enum_value);\n",
    "fixed-register-primary-lookup",
)
text = replace_once(
    text,
    "        if (function_id != MINIC_FUNCTION_INVALID) {\n",
    "        if (fixed_register_binding_id != MINIC_FIXED_REGISTER_BINDING_INVALID) {\n"
    "            if (!parse_fixed_register_reference(\n"
    "                    parser, name_span, fixed_register_binding_id, &primary_id)) {\n"
    "                return false;\n"
    "            }\n"
    "            return finish_value_expression(parser, primary_id, decay_array, expression_id);\n"
    "        }\n"
    "        if (function_id != MINIC_FUNCTION_INVALID) {\n",
    "fixed-register-primary-dispatch",
)
path.write_text(text)

# AST contracts and normalization know the new leaf expression.
path = Path("src/frontend/ast_verifier.c")
text = path.read_text()
case = r'''    case MINIC_EXPRESSION_FIXED_REGISTER: {
        const MinicFixedRegisterBinding *binding;

        binding = minic_c0_program_fixed_register_binding(
            program, expression->value.fixed_register_binding_id);
        return target != NULL && binding != NULL && expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, binding->type) &&
               (minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type)) &&
               minic_target_info_fixed_register_supported(
                   target, binding->register_name, binding->register_name_length);
    }
'''
text = replace_once(
    text,
    "    case MINIC_EXPRESSION_FUNCTION: {\n",
    case + "    case MINIC_EXPRESSION_FUNCTION: {\n",
    "fixed-register-verifier-case",
)
path.write_text(text)

path = Path("src/frontend/cast_normalization.c")
text = path.read_text()
text = replace_once(
    text,
    "    case MINIC_EXPRESSION_GLOBAL_OBJECT:\n    case MINIC_EXPRESSION_FUNCTION:\n",
    "    case MINIC_EXPRESSION_GLOBAL_OBJECT:\n    case MINIC_EXPRESSION_FIXED_REGISTER:\n    case MINIC_EXPRESSION_FUNCTION:\n",
    "fixed-register-normalization-leaf",
)
path.write_text(text)

# RV64 reads the architectural register directly; no symbol/address/storage exists.
path = Path("src/target/riscv64/codegen_expression.c")
text = path.read_text()
case = r'''    case MINIC_EXPRESSION_FIXED_REGISTER: {
        const MinicFixedRegisterBinding *binding;

        binding = minic_c0_program_fixed_register_binding(
            program, expression->value.fixed_register_binding_id);
        if (binding == NULL || binding->register_name_length == 0U ||
            (!minic_type_is_integer(binding->type) && !minic_type_is_pointer(binding->type)) ||
            fprintf(file, "  mv a0, %s\n", binding->register_name) < 0) {
            return false;
        }
        return minic_type_is_pointer(binding->type) ||
               minic_riscv64_emit_integer_conversion(file, binding->type, "a0");
    }
'''
text = replace_once(
    text,
    "    case MINIC_EXPRESSION_FUNCTION: {\n",
    case + "    case MINIC_EXPRESSION_FUNCTION: {\n",
    "fixed-register-codegen-case",
)
path.write_text(text)

# Linux-shaped focused regression and an explicit unsupported-target-register negative.
Path("tests/compiler/c0/gnu_fixed_register_bindings.c").write_text(
r'''struct task_struct;

register struct task_struct *riscv_current_is_tp __asm__("tp");
register unsigned long current_stack_pointer __asm__("sp");

struct task_struct *read_current_like(void) {
    return riscv_current_is_tp;
}

unsigned long read_stack_pointer_like(void) {
    return current_stack_pointer;
}
''')
Path("tests/compiler/c0/gnu_fixed_register_binding_reject.c").write_text(
r'''register unsigned long unsupported_binding __asm__("s1");
''')
Path("tests/compiler/c0/run-gnu-fixed-register-bindings.sh").write_text(
r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-fixed-register-bindings

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_fixed_register_bindings.c" \
    -o "$work/gnu_fixed_register_bindings.i"
"$minic" -S "$work/gnu_fixed_register_bindings.i" \
    -o "$work/gnu_fixed_register_bindings.s"

test -s "$work/gnu_fixed_register_bindings.s"
grep -F 'read_current_like:' "$work/gnu_fixed_register_bindings.s" >/dev/null
grep -F 'read_stack_pointer_like:' "$work/gnu_fixed_register_bindings.s" >/dev/null
grep -F '  mv a0, tp' "$work/gnu_fixed_register_bindings.s" >/dev/null
grep -F '  mv a0, sp' "$work/gnu_fixed_register_bindings.s" >/dev/null
! grep -F 'la a0, riscv_current_is_tp' "$work/gnu_fixed_register_bindings.s" >/dev/null
! grep -F 'la a0, current_stack_pointer' "$work/gnu_fixed_register_bindings.s" >/dev/null

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_fixed_register_binding_reject.c" \
    -o "$work/gnu_fixed_register_binding_reject.i"
set +e
"$minic" -S "$work/gnu_fixed_register_binding_reject.i" \
    -o "$work/gnu_fixed_register_binding_reject.s" \
    >"$work/reject.stdout" 2>"$work/reject.stderr"
status=$?
set -e
test "$status" -ne 0
grep -F 'fixed register binding is not supported by this target' "$work/reject.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_fixed_register_bindings storage=fixed-register-binding target=RV64 names=tp,sp read=direct-register memory-symbol=none unsupported=s1-reject'
''')

path = Path("tools/dev/pr76-focused.sh")
text = path.read_text()
anchor = 'sh tests/compiler/c0/run-gnu-inline-asm-named-operands.sh\n'
if anchor not in text:
    anchor = 'sh tests/compiler/c0/run-gnu-inline-asm-operands.sh\n'
text = replace_once(
    text,
    anchor,
    anchor + 'sh tests/compiler/c0/run-gnu-fixed-register-bindings.sh\n',
    "fixed-register-focused-gate",
)
path.write_text(text)
