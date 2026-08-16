#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 occurrence, found {count}")
    p.write_text(text.replace(old, new, 1))


def replace_n(path: str, old: str, new: str, n: int, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count < n:
        raise SystemExit(f"{label}: expected at least {n} occurrences, found {count}")
    p.write_text(text.replace(old, new, n))


def insert_before(path: str, marker: str, payload: str, label: str) -> None:
    replace_once(path, marker, payload + marker, label)


# Keep MinicLocal ABI/initialization untouched.  Local physical-register bindings
# are Program-owned side metadata keyed by LocalId.
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
    MinicLocalId local_id;
    bool is_local;
} MinicFixedRegisterBinding;
""",
    "binding-side-metadata",
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
                                                       MinicLocalId local_id,
                                                       const char *name,
                                                       size_t name_length,
                                                       const char *register_name,
                                                       size_t register_name_length,
                                                       MinicFixedRegisterBindingId *binding_id);
const MinicFixedRegisterBinding *minic_c0_program_local_fixed_register_binding(
    const MinicC0Program *program, MinicLocalId local_id);
""",
    "binding-api",
)

# Generalize the existing Program owner without allowing lexical bindings to
# pollute file-scope identifier conflicts.
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
    "name-conflict-scope",
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
                                       MinicLocalId local_id,
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
    binding.local_id = local_id;
    binding.is_local = is_local;
    *binding_id = program->fixed_register_binding_count;
    program->fixed_register_bindings[program->fixed_register_binding_count] = binding;
    program->fixed_register_binding_count += 1U;
    return true;
}

const MinicFixedRegisterBinding *minic_c0_program_local_fixed_register_binding(
    const MinicC0Program *program, MinicLocalId local_id) {
    size_t index;

    if (program == NULL || local_id >= program->local_count) {
        return NULL;
    }
    for (index = 0U; index < program->fixed_register_binding_count; ++index) {
        const MinicFixedRegisterBinding *binding;

        binding = &program->fixed_register_bindings[index];
        if (binding->is_local && binding->local_id == local_id) {
            return binding;
        }
    }
    return NULL;
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
                                      MINIC_LOCAL_INVALID,
                                      false,
                                      binding_id);
}

bool minic_c0_program_add_local_fixed_register_binding(MinicC0Program *program,
                                                       MinicLocalId local_id,
                                                       const char *name,
                                                       size_t name_length,
                                                       const char *register_name,
                                                       size_t register_name_length,
                                                       MinicFixedRegisterBindingId *binding_id) {
    const MinicLocal *local;

    local = minic_c0_program_local(program, local_id);
    if (local == NULL || local->is_array || minic_c0_program_local_fixed_register_binding(program, local_id) != NULL) {
        return false;
    }
    return add_fixed_register_binding(program,
                                      name,
                                      name_length,
                                      local->type,
                                      register_name,
                                      register_name_length,
                                      local_id,
                                      true,
                                      binding_id);
}
""",
    "binding-owner",
)

# File-scope fixed-register lookup must ignore lexical side metadata.
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
    "global-lookup-scope",
)

# Keep global tp/sp policy separate.  GNU local register variables used for
# RISC-V syscall/SBI extended asm bind the integer argument registers a0..a7.
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
    "target-api",
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
    "target-policy",
)

# Parse the optional asm("aN") suffix but leave Local itself unchanged.
insert_before(
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
    if (*length == 0U) {
        minic_parser_error(parser, "local fixed register name cannot be empty");
        return false;
    }
    buffer[*length] = '\0';
    *has_binding = true;
    return minic_parser_advance(parser) &&
           minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after local asm name");
}

''',
    "parser-helper",
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
    "parser-state",
)
replace_once(
    "src/frontend/parser_statement.c",
    """    if (minic_type_is_void(declared_type)) {
        minic_parser_error(parser, "local object cannot have void type");
        return false;
    }

    local.type = declared_type;
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
    if (has_fixed_register_binding && parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "local fixed register arrays are not supported");
        return false;
    }

    local.type = declared_type;
""",
    "parser-validate",
)
replace_n(
    "src/frontend/parser_statement.c",
    """        if (!minic_c0_program_add_local(parser->program, &local, &local_id)) {
            minic_parser_error(parser, "out of memory while adding local");
            return false;
        }
        if (!minic_parser_bind_local(parser, local.name_span, local_id)) {
            return false;
        }
""",
    """        if (!minic_c0_program_add_local(parser->program, &local, &local_id)) {
            minic_parser_error(parser, "out of memory while adding local");
            return false;
        }
        if (has_fixed_register_binding) {
            MinicFixedRegisterBindingId binding_id;

            if (!minic_c0_program_add_local_fixed_register_binding(
                    parser->program,
                    local_id,
                    parser->source + local.name_span.begin.offset,
                    minic_parser_span_length(local.name_span),
                    fixed_register_name,
                    fixed_register_name_length,
                    &binding_id)) {
                minic_parser_error(parser, "cannot record local fixed register binding");
                return false;
            }
            (void)binding_id;
        }
        if (!minic_parser_bind_local(parser, local.name_span, local_id)) {
            return false;
        }
""",
    2,
    "record-after-local-id",
)

# Verify side-table relationships without changing MinicLocal construction contracts.
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
        if (program->locals[index].element_count == 0U ||
            !type_is_valid(program, target, program->locals[index].type) ||
            minic_type_is_function(program->locals[index].type)) {
            return false;
        }
    }
    for (index = 0U; index < program->fixed_register_binding_count; ++index) {
        const MinicFixedRegisterBinding *binding;

        binding = &program->fixed_register_bindings[index];
        if (binding->is_local) {
            const MinicLocal *local;

            local = minic_c0_program_local(program, binding->local_id);
            if (local == NULL || local->is_array || !local->is_register_storage ||
                !minic_type_equal(local->type, binding->type) ||
                !minic_target_info_local_fixed_register_supported(
                    target, binding->register_name, binding->register_name_length)) {
                return false;
            }
        }
    }
""",
    "verify-side-table",
)

# Extended asm consumes the local binding side table.  General codegen still sees
# an ordinary Local and therefore requires no whole-function physical-reg liveness.
insert_before(
    "src/target/riscv64/codegen_inline_asm.c",
    "static bool assign_operand_registers(const MinicInlineAsm *inline_asm,\n",
    r'''static const MinicFixedRegisterBinding *
operand_local_fixed_register_binding(const MinicC0Program *program,
                                     const MinicInlineAsmOperand *operand) {
    const MinicExpression *expression;

    if (program == NULL || operand == NULL) {
        return NULL;
    }
    expression = minic_c0_program_expression(program, operand->expression);
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL) {
        return NULL;
    }
    return minic_c0_program_local_fixed_register_binding(program, expression->value.local_id);
}

static bool operand_accepts_register(const MinicInlineAsmOperand *operand) {
    return constraint_is(operand, "r") || constraint_is(operand, "rJ") ||
           constraint_is(operand, "rK") || constraint_is(operand, "=r") ||
           constraint_is(operand, "=&r") || constraint_is(operand, "+r");
}

''',
    "asm-helper",
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
    "asm-allocator",
)

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
"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_local_fixed_register_bindings.c" -o "$work/ok.i"
"$minic" -S "$work/ok.i" -o "$work/ok.s"
grep -F 'add a0, a0, a1 # nr=a7' "$work/ok.s" >/dev/null
grep -F 'mv a0, a0' "$work/ok.s" >/dev/null
"$rv_cc" -c "$work/ok.s" -o "$work/ok.o"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_local_fixed_register_binding_reject.c" -o "$work/reject.i"
set +e
"$minic" -S "$work/reject.i" -o "$work/reject.s" >"$work/reject.stdout" 2>"$work/reject.stderr"
status=$?
set -e
test "$status" -ne 0
grep -F 'local fixed register binding is not supported by this target' "$work/reject.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_local_fixed_register_bindings owner=side-table local-abi=unchanged asm-binding=a0,a1,a7'
''')

print("staged local fixed-register bindings as Program-owned LocalId side metadata")
