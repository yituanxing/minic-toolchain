#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one replacement, found {count}")
    target.write_text(text.replace(old, new, 1))


def insert_before_once(path: str, marker: str, payload: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(marker)
    if count != 1:
        raise SystemExit(f"{label}: expected one marker, found {count}")
    target.write_text(text.replace(marker, payload + marker, 1))


# Local register variables remain ordinary lexical locals.  The fixed-register
# binding is metadata used when the local participates in GNU extended asm;
# ordinary reads/writes continue to use the existing Local storage owner.
replace_once(
    "src/frontend/ast.h",
    """typedef struct MinicLocal {
    MinicSourceSpan name_span;
    MinicType type;
    size_t element_count;
    bool is_array;
    bool is_register_storage;
} MinicLocal;
""",
    """typedef struct MinicLocal {
    MinicSourceSpan name_span;
    MinicType type;
    size_t element_count;
    MinicFixedRegisterBindingId fixed_register_binding_id;
    bool is_array;
    bool is_register_storage;
    bool has_fixed_register_binding;
} MinicLocal;
""",
    "local-fixed-register-metadata",
)
replace_once(
    "src/frontend/ast.h",
    """typedef struct MinicFixedRegisterBinding {
    char *name;
    size_t name_length;
    char *register_name;
    size_t register_name_length;
    MinicType type;
} MinicFixedRegisterBinding;
""",
    """typedef struct MinicFixedRegisterBinding {
    char *name;
    size_t name_length;
    char *register_name;
    size_t register_name_length;
    MinicType type;
    bool is_local;
} MinicFixedRegisterBinding;
""",
    "fixed-register-binding-scope",
)
replace_once(
    "src/frontend/ast.h",
    """bool minic_c0_program_add_fixed_register_binding(MinicC0Program *program,
                                                 const char *name,
                                                 size_t name_length,
                                                 MinicType type,
                                                 const char *register_name,
                                                 size_t register_name_length,
                                                 MinicFixedRegisterBindingId *binding_id);
""",
    """bool minic_c0_program_add_fixed_register_binding(MinicC0Program *program,
                                                 const char *name,
                                                 size_t name_length,
                                                 MinicType type,
                                                 const char *register_name,
                                                 size_t register_name_length,
                                                 MinicFixedRegisterBindingId *binding_id);
bool minic_c0_program_add_local_fixed_register_binding(MinicC0Program *program,
                                                       const char *name,
                                                       size_t name_length,
                                                       MinicType type,
                                                       const char *register_name,
                                                       size_t register_name_length,
                                                       MinicFixedRegisterBindingId *binding_id);
""",
    "local-fixed-register-api",
)

# Generalize Program-owned binding storage without allowing local binding names
# to contaminate file-scope ordinary-identifier lookup or conflicts.
replace_once(
    "src/frontend/ast_global.c",
    """        binding = &program->fixed_register_bindings[index];
        if (binding->name_length == name_length && memcmp(binding->name, name, name_length) == 0) {
            return true;
        }
""",
    """        binding = &program->fixed_register_bindings[index];
        if (!binding->is_local && binding->name_length == name_length &&
            memcmp(binding->name, name, name_length) == 0) {
            return true;
        }
""",
    "file-scope-conflicts-ignore-local-bindings",
)
replace_once(
    "src/frontend/ast_global.c",
    """bool minic_c0_program_add_fixed_register_binding(MinicC0Program *program,
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
""",
    """static bool add_fixed_register_binding(MinicC0Program *program,
                                       const char *name,
                                       size_t name_length,
                                       MinicType type,
                                       const char *register_name,
                                       size_t register_name_length,
                                       bool is_local,
                                       MinicFixedRegisterBindingId *binding_id) {
    MinicFixedRegisterBinding binding;

    if (program == NULL || name == NULL || register_name == NULL || binding_id == NULL ||
        register_name_length == 0U ||
        (!minic_type_is_integer(type) && !minic_type_is_pointer(type)) ||
        (!is_local && name_conflicts(program, name, name_length)) ||
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
    binding.is_local = is_local;
    *binding_id = program->fixed_register_binding_count;
    program->fixed_register_bindings[program->fixed_register_binding_count] = binding;
    program->fixed_register_binding_count += 1U;
    return true;
}

bool minic_c0_program_add_fixed_register_binding(MinicC0Program *program,
                                                 const char *name,
                                                 size_t name_length,
                                                 MinicType type,
                                                 const char *register_name,
                                                 size_t register_name_length,
                                                 MinicFixedRegisterBindingId *binding_id) {
    return add_fixed_register_binding(program,
                                      name,
                                      name_length,
                                      type,
                                      register_name,
                                      register_name_length,
                                      false,
                                      binding_id);
}

bool minic_c0_program_add_local_fixed_register_binding(MinicC0Program *program,
                                                       const char *name,
                                                       size_t name_length,
                                                       MinicType type,
                                                       const char *register_name,
                                                       size_t register_name_length,
                                                       MinicFixedRegisterBindingId *binding_id) {
    return add_fixed_register_binding(program,
                                      name,
                                      name_length,
                                      type,
                                      register_name,
                                      register_name_length,
                                      true,
                                      binding_id);
}
""",
    "generalize-fixed-register-binding-owner",
)

replace_once(
    "src/frontend/parser_global.c",
    """        binding = minic_c0_program_fixed_register_binding(parser->program, index);
        if (binding != NULL && binding->name_length == name_length &&
            memcmp(binding->name, parser->source + name_span.begin.offset, name_length) == 0) {
            return index;
        }
""",
    """        binding = minic_c0_program_fixed_register_binding(parser->program, index);
        if (binding != NULL && !binding->is_local && binding->name_length == name_length &&
            memcmp(binding->name, parser->source + name_span.begin.offset, name_length) == 0) {
            return index;
        }
""",
    "fixed-register-lookup-ignore-local",
)

# TargetInfo keeps the existing file-scope tp/sp contract separate from the
# local extended-asm binding class.  Linux syscall/SBI shapes require a0..a7.
replace_once(
    "src/target/target_info.h",
    """bool minic_target_info_fixed_register_supported(const MinicTargetInfo *target,
                                                const char *name,
                                                size_t name_length);
""",
    """bool minic_target_info_fixed_register_supported(const MinicTargetInfo *target,
                                                const char *name,
                                                size_t name_length);
bool minic_target_info_local_fixed_register_supported(const MinicTargetInfo *target,
                                                      const char *name,
                                                      size_t name_length);
""",
    "local-fixed-register-target-api",
)
replace_once(
    "src/target/target_info.c",
    """bool minic_target_info_inline_asm_register_clobber_supported(const MinicTargetInfo *target,
""",
    """bool minic_target_info_local_fixed_register_supported(const MinicTargetInfo *target,
                                                      const char *name,
                                                      size_t name_length) {
    if (target == NULL || name == NULL) {
        return false;
    }
    return name_length == 2U && name[0] == 'a' && name[1] >= '0' && name[1] <= '7';
}

bool minic_target_info_inline_asm_register_clobber_supported(const MinicTargetInfo *target,
""",
    "local-fixed-register-target-policy",
)

# Parse `register T x asm("aN")` as an ordinary lexical Local plus an explicit
# binding descriptor.  The Local remains stack-backed outside extended asm.
insert_before_once(
    "src/frontend/parser_statement.c",
    "static bool local_declarator_starts_function_pointer(const MinicParser *parser) {\n",
    r'''static bool local_identifier_is(const MinicParser *parser, const char *name) {
    size_t name_length;

    if (parser == NULL || name == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    name_length = strlen(name);
    return minic_parser_span_length(parser->current.span) == name_length &&
           memcmp(parser->source + parser->current.span.begin.offset, name, name_length) == 0;
}

static bool parse_local_fixed_register_name(MinicParser *parser,
                                            char *buffer,
                                            size_t capacity,
                                            size_t *length,
                                            bool *has_binding) {
    size_t cursor;
    size_t end;

    if (parser == NULL || buffer == NULL || capacity == 0U || length == NULL ||
        has_binding == NULL) {
        return false;
    }
    *length = 0U;
    *has_binding = false;
    if (!local_identifier_is(parser, "asm") && !local_identifier_is(parser, "__asm") &&
        !local_identifier_is(parser, "__asm__")) {
        return true;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after local asm") ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "local fixed register requires one string literal");
        }
        return false;
    }
    if (parser->current.span.end.offset <= parser->current.span.begin.offset + 1U) {
        minic_parser_error(parser, "local fixed register name cannot be empty");
        return false;
    }
    cursor = parser->current.span.begin.offset + 1U;
    end = parser->current.span.end.offset - 1U;
    while (cursor < end) {
        if (parser->source[cursor] == '\\') {
            minic_parser_error(parser, "escaped local fixed register names are not supported yet");
            return false;
        }
        if (*length + 1U >= capacity) {
            minic_parser_error(parser, "local fixed register name is too long");
            return false;
        }
        buffer[*length] = parser->source[cursor];
        *length += 1U;
        cursor += 1U;
    }
    buffer[*length] = '\0';
    *has_binding = true;
    return minic_parser_advance(parser) &&
           minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after local asm name");
}

''',
    "local-fixed-register-parser-helper",
)
replace_once(
    "src/frontend/parser_statement.c",
    """    MinicLocalObjectAttributes attributes;
    MinicType declared_type;

    (void)memset(&attributes, 0, sizeof(attributes));
""",
    """    MinicLocalObjectAttributes attributes;
    MinicType declared_type;
    char fixed_register_name[32];
    size_t fixed_register_name_length;
    bool has_fixed_register_binding;

    (void)memset(&attributes, 0, sizeof(attributes));
    (void)memset(fixed_register_name, 0, sizeof(fixed_register_name));
    fixed_register_name_length = 0U;
    has_fixed_register_binding = false;
""",
    "local-fixed-register-declarator-state",
)
replace_once(
    "src/frontend/parser_statement.c",
    """    if (minic_type_is_void(declared_type)) {
        minic_parser_error(parser, "local object cannot have void type");
        return false;
    }

    local.type = declared_type;
    local.element_count = 1U;
    local.is_array = false;
    local.is_register_storage = is_register_storage;
    if (minic_parser_name_bound_in_current_scope(parser, local.name_span)) {
""",
    """    if (minic_type_is_void(declared_type)) {
        minic_parser_error(parser, "local object cannot have void type");
        return false;
    }
    if (!parse_local_fixed_register_name(parser,
                                         fixed_register_name,
                                         sizeof(fixed_register_name),
                                         &fixed_register_name_length,
                                         &has_fixed_register_binding)) {
        return false;
    }
    if (has_fixed_register_binding && !is_register_storage) {
        minic_parser_error(parser, "local fixed register binding requires register storage class");
        return false;
    }
    if (has_fixed_register_binding &&
        (!minic_type_is_integer(declared_type) && !minic_type_is_pointer(declared_type))) {
        minic_parser_error(parser, "local fixed register binding requires scalar integer or pointer type");
        return false;
    }
    if (has_fixed_register_binding &&
        !minic_target_info_local_fixed_register_supported(
            parser->target_info, fixed_register_name, fixed_register_name_length)) {
        minic_parser_error(parser, "local fixed register binding is not supported by this target");
        return false;
    }

    local.type = declared_type;
    local.element_count = 1U;
    local.fixed_register_binding_id = MINIC_FIXED_REGISTER_BINDING_INVALID;
    local.is_array = false;
    local.is_register_storage = is_register_storage;
    local.has_fixed_register_binding = false;
    if (minic_parser_name_bound_in_current_scope(parser, local.name_span)) {
""",
    "local-fixed-register-parse-and-validate",
)
replace_once(
    "src/frontend/parser_statement.c",
    """        minic_parser_error(parser, "duplicate local declaration");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
""",
    """        minic_parser_error(parser, "duplicate local declaration");
        return false;
    }
    if (has_fixed_register_binding && parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "local fixed register arrays are not supported");
        return false;
    }
    if (has_fixed_register_binding) {
        if (!minic_c0_program_add_local_fixed_register_binding(
                parser->program,
                parser->source + local.name_span.begin.offset,
                minic_parser_span_length(local.name_span),
                local.type,
                fixed_register_name,
                fixed_register_name_length,
                &local.fixed_register_binding_id)) {
            minic_parser_error(parser, "cannot record local fixed register binding");
            return false;
        }
        local.has_fixed_register_binding = true;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
""",
    "local-fixed-register-record-binding",
)

# Verify that Local->binding relationships stay lexical and target-valid.
replace_once(
    "src/frontend/ast_verifier.c",
    """    for (index = 0U; index < program->local_count; ++index) {
        if (program->locals[index].element_count == 0U ||
            !type_is_valid(program, target, program->locals[index].type) ||
            minic_type_is_function(program->locals[index].type)) {
            return false;
        }
    }
""",
    """    for (index = 0U; index < program->local_count; ++index) {
        const MinicLocal *local;

        local = &program->locals[index];
        if (local->element_count == 0U || !type_is_valid(program, target, local->type) ||
            minic_type_is_function(local->type)) {
            return false;
        }
        if (local->has_fixed_register_binding) {
            const MinicFixedRegisterBinding *binding;

            binding = minic_c0_program_fixed_register_binding(
                program, local->fixed_register_binding_id);
            if (!local->is_register_storage || local->is_array || binding == NULL ||
                !binding->is_local || !minic_type_equal(binding->type, local->type) ||
                !minic_target_info_local_fixed_register_supported(
                    target, binding->register_name, binding->register_name_length)) {
                return false;
            }
        }
    }
""",
    "verify-local-fixed-register-binding",
)

# Extended asm is the only place where local physical binding takes effect.
# Normal Local storage remains unchanged; input/output staging therefore reuses
# the existing stack-backed value semantics and only the operand allocator is constrained.
insert_before_once(
    "src/target/riscv64/codegen_inline_asm.c",
    "static bool assign_operand_registers(const MinicInlineAsm *inline_asm,\n",
    r'''static const MinicFixedRegisterBinding *
operand_local_fixed_register_binding(const MinicC0Program *program,
                                     const MinicInlineAsmOperand *operand) {
    const MinicExpression *expression;
    const MinicLocal *local;
    const MinicFixedRegisterBinding *binding;

    if (program == NULL || operand == NULL) {
        return NULL;
    }
    expression = minic_c0_program_expression(program, operand->expression);
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL) {
        return NULL;
    }
    local = minic_c0_program_local(program, expression->value.local_id);
    if (local == NULL || !local->has_fixed_register_binding) {
        return NULL;
    }
    binding = minic_c0_program_fixed_register_binding(program, local->fixed_register_binding_id);
    return binding != NULL && binding->is_local ? binding : NULL;
}

static bool operand_accepts_register(const MinicInlineAsmOperand *operand) {
    return constraint_is(operand, "r") || constraint_is(operand, "rJ") ||
           constraint_is(operand, "rK") || constraint_is(operand, "=r") ||
           constraint_is(operand, "=&r") || constraint_is(operand, "+r");
}

''',
    "inline-asm-local-fixed-register-helper",
)
replace_once(
    "src/target/riscv64/codegen_inline_asm.c",
    """static bool assign_operand_registers(const MinicInlineAsm *inline_asm,
                                     const MinicC0Program *program,
                                     const char **operand_registers,
                                     size_t operand_count) {
    size_t candidate_index;
    size_t operand_index;

    if (inline_asm == NULL || program == NULL || operand_registers == NULL) {
        return false;
    }
    candidate_index = 0U;
    for (operand_index = 0U; operand_index < operand_count; ++operand_index) {
        const MinicInlineAsmOperand *operand;

        operand = operand_at(inline_asm, operand_index);
        if (operand == NULL) {
            return false;
        }
        if (operand_index >= inline_asm->output_count) {
            size_t matching_output_index;

            if (constraint_matching_output(inline_asm, operand, &matching_output_index)) {
                if (!matching_output_is_register(inline_asm, matching_output_index) ||
                    operand_registers[matching_output_index] == NULL) {
                    return false;
                }
                operand_registers[operand_index] = operand_registers[matching_output_index];
                continue;
            }
        }
        if (operand_uses_immediate(program, operand)) {
            operand_registers[operand_index] = NULL;
            continue;
        }
        while (candidate_index < MINIC_RISCV64_INLINE_ASM_REGISTER_COUNT &&
               (inline_asm_clobbers_register(
                    inline_asm, minic_riscv64_inline_asm_registers[candidate_index].name) ||
                (inline_asm->is_goto &&
                 minic_riscv64_inline_asm_registers[candidate_index].is_callee_saved))) {
            candidate_index += 1U;
        }
        if (candidate_index >= MINIC_RISCV64_INLINE_ASM_REGISTER_COUNT) {
            return false;
        }
        operand_registers[operand_index] = minic_riscv64_inline_asm_registers[candidate_index].name;
        candidate_index += 1U;
    }
    return true;
}
""",
    """static bool assign_operand_registers(const MinicInlineAsm *inline_asm,
                                     const MinicC0Program *program,
                                     const char **operand_registers,
                                     size_t operand_count) {
    size_t candidate_index;
    size_t operand_index;

    if (inline_asm == NULL || program == NULL || operand_registers == NULL) {
        return false;
    }
    candidate_index = 0U;
    for (operand_index = 0U; operand_index < operand_count; ++operand_index) {
        const MinicInlineAsmOperand *operand;
        const MinicFixedRegisterBinding *binding;

        operand = operand_at(inline_asm, operand_index);
        if (operand == NULL) {
            return false;
        }
        binding = operand_local_fixed_register_binding(program, operand);
        if (operand_index >= inline_asm->output_count) {
            size_t matching_output_index;

            if (constraint_matching_output(inline_asm, operand, &matching_output_index)) {
                const char *matched_register;

                if (!matching_output_is_register(inline_asm, matching_output_index) ||
                    operand_registers[matching_output_index] == NULL) {
                    return false;
                }
                matched_register = operand_registers[matching_output_index];
                if (binding != NULL &&
                    (strlen(matched_register) != binding->register_name_length ||
                     memcmp(matched_register,
                            binding->register_name,
                            binding->register_name_length) != 0)) {
                    return false;
                }
                operand_registers[operand_index] = matched_register;
                continue;
            }
        }
        if (operand_uses_immediate(program, operand)) {
            operand_registers[operand_index] = NULL;
            continue;
        }
        if (binding != NULL) {
            if (!operand_accepts_register(operand) ||
                inline_asm_clobbers_register(inline_asm, binding->register_name)) {
                return false;
            }
            operand_registers[operand_index] = binding->register_name;
            continue;
        }
        while (candidate_index < MINIC_RISCV64_INLINE_ASM_REGISTER_COUNT &&
               (inline_asm_clobbers_register(
                    inline_asm, minic_riscv64_inline_asm_registers[candidate_index].name) ||
                (inline_asm->is_goto &&
                 minic_riscv64_inline_asm_registers[candidate_index].is_callee_saved))) {
            candidate_index += 1U;
        }
        if (candidate_index >= MINIC_RISCV64_INLINE_ASM_REGISTER_COUNT) {
            return false;
        }
        operand_registers[operand_index] = minic_riscv64_inline_asm_registers[candidate_index].name;
        candidate_index += 1U;
    }
    return true;
}
""",
    "inline-asm-honor-local-fixed-registers",
)

# Permanent focused coverage: same physical a0 is intentionally shared by one
# write-only output and one input, as in Linux vDSO syscall fallbacks.  Reusing
# local source names in another function freezes lexical ownership/no leakage.
Path("tests/compiler/c0/gnu_local_fixed_register_bindings.c").write_text(r'''long local_fixed_syscall_like(long first, long second) {
    register long a0v asm("a0") = first;
    register long a1v asm("a1") = second;
    register long result asm("a0");
    register long nr asm("a7") = 169;

    asm volatile("add %0, %1, %2 # nr=%3"
                 : "=r"(result)
                 : "r"(a0v), "r"(a1v), "r"(nr)
                 : "memory");
    return result;
}

void *local_fixed_pointer_like(void *input) {
    register void *a0v asm("a0") = input;
    register void *result asm("a0");

    asm volatile("mv %0, %1" : "=r"(result) : "r"(a0v) : "memory");
    return result;
}
''')
Path("tests/compiler/c0/gnu_local_fixed_register_binding_reject.c").write_text(r'''long reject_local_fixed(long value) {
    register long bad asm("s1") = value;
    return bad;
}
''')
Path("tests/compiler/c0/run-gnu-local-fixed-register-bindings.sh").write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
rv_cc=${RV_CC:-riscv64-linux-gnu-gcc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-local-fixed-register-bindings

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_local_fixed_register_bindings.c" \
    -o "$work/gnu_local_fixed_register_bindings.i"
"$minic" -S "$work/gnu_local_fixed_register_bindings.i" \
    -o "$work/gnu_local_fixed_register_bindings.s"

test -s "$work/gnu_local_fixed_register_bindings.s"
grep -F 'add a0, a0, a1 # nr=a7' "$work/gnu_local_fixed_register_bindings.s" >/dev/null
grep -F 'mv a0, a0' "$work/gnu_local_fixed_register_bindings.s" >/dev/null
"$rv_cc" -c "$work/gnu_local_fixed_register_bindings.s" \
    -o "$work/gnu_local_fixed_register_bindings.o"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_local_fixed_register_binding_reject.c" \
    -o "$work/gnu_local_fixed_register_binding_reject.i"
set +e
"$minic" -S "$work/gnu_local_fixed_register_binding_reject.i" \
    -o "$work/gnu_local_fixed_register_binding_reject.s" \
    >"$work/reject.stdout" 2>"$work/reject.stderr"
status=$?
set -e
test "$status" -ne 0
grep -F 'local fixed register binding is not supported by this target' \
    "$work/reject.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_local_fixed_register_bindings scope=local storage=ordinary-local asm-binding=a0,a1,a7 overlap=a0 output+input target-policy=local-only'
''')

print("staged GNU local fixed-register variables as Local-owned asm operand constraints")
