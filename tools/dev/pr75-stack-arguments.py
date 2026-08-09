#!/usr/bin/env python3
from pathlib import Path

MAX = "MINIC_MAX_FUNCTION_PARAMETERS"


def load(path):
    return Path(path), Path(path).read_text()


def replace_exact(text, old, new, label, expected=1):
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"{label}: expected {expected} matches, found {count}")
    return text.replace(old, new)


# Shared AST capacity. This is a representation limit, not the RV64 register count.
path, text = load("src/frontend/ast.h")
anchor = "#define MINIC_GLOBAL_OBJECT_INVALID ((MinicGlobalObjectId) - 1)\n"
if f"#define {MAX} 16U" not in text:
    text = replace_exact(
        text,
        anchor,
        anchor + f"#define {MAX} 16U\n",
        "ast max-parameter anchor",
    )
text = replace_exact(
    text,
    "MinicExpressionId arguments[8];",
    f"MinicExpressionId arguments[{MAX}];",
    "call argument storage",
)
text = replace_exact(
    text,
    "MinicType parameter_types[8];",
    f"MinicType parameter_types[{MAX}];",
    "function/function-type parameter storage",
    expected=2,
)
path.write_text(text)

# Program/function-type storage.
path, text = load("src/frontend/ast.c")
text = replace_exact(
    text,
    "MinicType normalized_parameter_types[8];",
    f"MinicType normalized_parameter_types[{MAX}];",
    "normalized parameter storage",
    expected=2,
)
text = replace_exact(
    text,
    "MinicType parameter_types[8];",
    f"MinicType parameter_types[{MAX}];",
    "default parameter storage",
)
text = text.replace("parameter_count > 8U", f"parameter_count > {MAX}")
text = replace_exact(
    text,
    "for (parameter_index = 0U; parameter_index < 8U; ++parameter_index)",
    f"for (parameter_index = 0U; parameter_index < {MAX}; ++parameter_index)",
    "signature reset loop",
)
path.write_text(text)

# Function declarations/definitions and nested function-pointer declarators.
path, text = load("src/frontend/parser_function.c")
text = replace_exact(
    text,
    "MinicType nested_parameter_types[8];",
    f"MinicType nested_parameter_types[{MAX}];",
    "nested function pointer parameters",
)
text = replace_exact(
    text,
    "MinicSourceSpan parameter_name_spans[8];",
    f"MinicSourceSpan parameter_name_spans[{MAX}];",
    "parameter name storage",
)
text = replace_exact(
    text,
    "MinicType parameter_types[8];",
    f"MinicType parameter_types[{MAX}];",
    "function parameter storage",
)
text = replace_exact(
    text,
    "if (*parameter_count >= 8U) {",
    f"if (*parameter_count >= {MAX}) {{",
    "parameter parser capacity",
)
path.write_text(text)

for filename, label in [
    ("src/frontend/parser_typedef.c", "function pointer typedef parameters"),
    ("src/frontend/parser_record.c", "function pointer record-field parameters"),
]:
    path, text = load(filename)
    text = replace_exact(
        text,
        "MinicType parameter_types[8];",
        f"MinicType parameter_types[{MAX}];",
        label,
    )
    path.write_text(text)

# Direct and indirect call AST construction. Variadic calls intentionally keep the
# existing eight-argument limit until va_start can bridge register and stack areas.
path, text = load("src/frontend/parser_expression.c")
text = replace_exact(
    text,
    "if (argument_index >= 8U ||",
    f"if (argument_index >= {MAX} ||",
    "direct call argument capacity",
)
text = text.replace("callee->parameter_count > 8U", f"callee->parameter_count > {MAX}")
path.write_text(text)

path, text = load("src/frontend/parser_postfix.c")
text = text.replace("function_type->parameter_count > 8U", f"function_type->parameter_count > {MAX}")
path.write_text(text)

# Normalization/verifier representation bounds.
for filename in ["src/frontend/cast_normalization.c", "src/frontend/ast_verifier.c"]:
    path, text = load(filename)
    text = text.replace("argument_count > 8U", f"argument_count > {MAX}")
    text = text.replace("parameter_count > 8U", f"parameter_count > {MAX}")
    path.write_text(text)

# Frame layout can host more fixed parameters. Variadic fixed integer parameters
# remain register-only because the current va_start model saves the register area.
path, text = load("src/target/riscv64/codegen_support.c")
text = replace_exact(
    text,
    "if (program == NULL || function == NULL || layout == NULL || function->parameter_count > 8U) {",
    f"if (program == NULL || function == NULL || layout == NULL ||\n        function->parameter_count > {MAX}) {{",
    "frame parameter capacity",
)
text = replace_exact(
    text,
    "if (integer_parameter_count > 8U) {\n        return false;\n    }",
    "if (function->is_variadic && integer_parameter_count > 8U) {\n        return false;\n    }",
    "variadic integer-register boundary",
)
path.write_text(text)

# Callee side: spill register parameters as before; fixed integer/pointer parameters
# beyond a7 are loaded from the caller's stack argument area.
path, text = load("src/target/riscv64/codegen_function.c")
text = replace_exact(
    text,
    "    if (success && function->parameter_count > 8U) {\n        return false;\n    }\n",
    "",
    "remove eight-parameter callee rejection",
)
old = '''    if (success) {
        size_t parameter_index;
        size_t integer_register_index;
        size_t floating_register_index;

        integer_register_index = 0U;
        floating_register_index = 0U;

        for (parameter_index = 0U; success && parameter_index < function->parameter_count;
             ++parameter_index) {
            const MinicLocal *parameter;
            MinicLocalId local_id;

            local_id = function->local_begin + parameter_index;
            parameter = minic_c0_program_local(program, local_id);
            if (parameter == NULL) {
                success = false;
                break;
            }
            if (minic_type_is_double(parameter->type) || minic_type_is_float(parameter->type)) {
                if (floating_register_index >= 8U) {
                    success = false;
                    break;
                }
                success = fprintf(file,
                                  minic_type_is_double(parameter->type) ? "  fmv.x.d t0, fa%zu\\n"
                                                                        : "  fmv.x.w t0, fa%zu\\n",
                                  floating_register_index) >= 0 &&
                          minic_riscv64_emit_object_store_register(
                              file, program, function, local_id, "t0");
                floating_register_index += 1U;
            } else {
                if (integer_register_index >= 8U) {
                    success = false;
                    break;
                }
                success = minic_riscv64_emit_object_store_register(
                    file,
                    program,
                    function,
                    local_id,
                    minic_riscv64_argument_registers[integer_register_index]);
                integer_register_index += 1U;
            }
        }
    }
'''
new = '''    if (success) {
        size_t parameter_index;
        size_t integer_register_index;
        size_t floating_register_index;
        size_t stack_parameter_index;

        integer_register_index = 0U;
        floating_register_index = 0U;
        stack_parameter_index = 0U;

        for (parameter_index = 0U; success && parameter_index < function->parameter_count;
             ++parameter_index) {
            const MinicLocal *parameter;
            MinicLocalId local_id;

            local_id = function->local_begin + parameter_index;
            parameter = minic_c0_program_local(program, local_id);
            if (parameter == NULL) {
                success = false;
                break;
            }
            if (minic_type_is_double(parameter->type) || minic_type_is_float(parameter->type)) {
                if (floating_register_index >= 8U) {
                    success = false;
                    break;
                }
                success = fprintf(file,
                                  minic_type_is_double(parameter->type) ? "  fmv.x.d t0, fa%zu\\n"
                                                                        : "  fmv.x.w t0, fa%zu\\n",
                                  floating_register_index) >= 0 &&
                          minic_riscv64_emit_object_store_register(
                              file, program, function, local_id, "t0");
                floating_register_index += 1U;
            } else if (integer_register_index < 8U) {
                success = minic_riscv64_emit_object_store_register(
                    file,
                    program,
                    function,
                    local_id,
                    minic_riscv64_argument_registers[integer_register_index]);
                integer_register_index += 1U;
            } else {
                size_t incoming_offset;

                if (stack_parameter_index > (SIZE_MAX - frame_size) / 8U) {
                    success = false;
                    break;
                }
                incoming_offset = frame_size + stack_parameter_index * 8U;
                success = minic_riscv64_emit_sp_load64(file, "t0", incoming_offset) &&
                          minic_riscv64_emit_object_store_register(
                              file, program, function, local_id, "t0");
                stack_parameter_index += 1U;
            }
        }
    }
'''
text = replace_exact(text, old, new, "callee parameter materialization")
path.write_text(text)

# Caller side: evaluate all arguments into the existing 16-byte temporary slots,
# reserve an aligned outgoing stack-argument area, populate a0-a7 and stack slots,
# call while temporaries remain above the outgoing area, then release both areas.
path, text = load("src/target/riscv64/codegen_expression.c")
text = replace_exact(
    text,
    "        size_t temporary_bytes;\n        bool is_indirect;",
    "        size_t outgoing_stack_bytes;\n        size_t stack_argument_count;\n        size_t temporary_bytes;\n        bool is_indirect;",
    "call stack-argument declarations",
)
text = text.replace("indirect_type->parameter_count > 8U", f"indirect_type->parameter_count > {MAX}")
text = text.replace("direct_callee->parameter_count > 8U", f"direct_callee->parameter_count > {MAX}")
text = text.replace("argument_count > 8U", f"argument_count > {MAX}")
text = replace_exact(
    text,
    "        argument_count = expression->value.call.argument_count;\n",
    "        argument_count = expression->value.call.argument_count;\n        outgoing_stack_bytes = 0U;\n        stack_argument_count = 0U;\n",
    "call stack-argument initialization",
)
marker = '''        {
            size_t integer_register_index;
            size_t floating_register_index;

            integer_register_index = 0U;
            floating_register_index = 0U;
            for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
                size_t offset;
                bool fixed_floating;

                offset = (argument_count - 1U - argument_index) * 16U;
                fixed_floating = argument_index < parameter_count &&
                                 (minic_type_is_double(parameter_types[argument_index]) ||
                                  minic_type_is_float(parameter_types[argument_index]));
                if (fixed_floating) {
                    if (floating_register_index >= 8U ||
                        fprintf(file,
                                minic_type_is_double(parameter_types[argument_index])
                                    ? "  ld t0, %zu(sp)\\n  fmv.d.x fa%zu, t0\\n"
                                    : "  ld t0, %zu(sp)\\n  fmv.w.x fa%zu, t0\\n",
                                offset,
                                floating_register_index) < 0) {
                        return false;
                    }
                    floating_register_index += 1U;
                } else {
                    if (integer_register_index >= 8U ||
                        fprintf(file, "  ld a%zu, %zu(sp)\\n", integer_register_index, offset) < 0) {
                        return false;
                    }
                    integer_register_index += 1U;
                }
            }
        }
'''
replacement = '''        {
            size_t integer_register_index;
            size_t floating_register_index;

            integer_register_index = 0U;
            floating_register_index = 0U;
            for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
                bool fixed_floating;

                fixed_floating = argument_index < parameter_count &&
                                 (minic_type_is_double(parameter_types[argument_index]) ||
                                  minic_type_is_float(parameter_types[argument_index]));
                if (fixed_floating) {
                    if (floating_register_index >= 8U) {
                        return false;
                    }
                    floating_register_index += 1U;
                } else if (integer_register_index < 8U) {
                    integer_register_index += 1U;
                } else {
                    stack_argument_count += 1U;
                }
            }
        }
        if (stack_argument_count > (SIZE_MAX - 15U) / 8U) {
            return false;
        }
        outgoing_stack_bytes = (stack_argument_count * 8U + 15U) & ~(size_t)15U;
        if (outgoing_stack_bytes != 0U &&
            !minic_riscv64_emit_stack_allocate(file, outgoing_stack_bytes)) {
            return false;
        }
        {
            size_t integer_register_index;
            size_t floating_register_index;
            size_t stack_argument_index;

            integer_register_index = 0U;
            floating_register_index = 0U;
            stack_argument_index = 0U;
            for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
                size_t offset;
                bool fixed_floating;

                offset = outgoing_stack_bytes + (argument_count - 1U - argument_index) * 16U;
                fixed_floating = argument_index < parameter_count &&
                                 (minic_type_is_double(parameter_types[argument_index]) ||
                                  minic_type_is_float(parameter_types[argument_index]));
                if (fixed_floating) {
                    if (floating_register_index >= 8U ||
                        fprintf(file,
                                minic_type_is_double(parameter_types[argument_index])
                                    ? "  ld t0, %zu(sp)\\n  fmv.d.x fa%zu, t0\\n"
                                    : "  ld t0, %zu(sp)\\n  fmv.w.x fa%zu, t0\\n",
                                offset,
                                floating_register_index) < 0) {
                        return false;
                    }
                    floating_register_index += 1U;
                } else if (integer_register_index < 8U) {
                    if (fprintf(file, "  ld a%zu, %zu(sp)\\n", integer_register_index, offset) < 0) {
                        return false;
                    }
                    integer_register_index += 1U;
                } else {
                    if (minic_riscv64_emit_sp_load64(file, "t0", offset) == false ||
                        minic_riscv64_emit_sp_store64(file, "t0", stack_argument_index * 8U) == false) {
                        return false;
                    }
                    stack_argument_index += 1U;
                }
            }
        }
'''
text = replace_exact(text, marker, replacement, "caller register/stack assignment")
text = replace_exact(
    text,
    "            if (fprintf(file, \"  ld t0, %zu(sp)\\n\", argument_count * 16U) < 0) {",
    "            if (fprintf(file,\n                        \"  ld t0, %zu(sp)\\n\",\n                        outgoing_stack_bytes + argument_count * 16U) < 0) {",
    "indirect callee temporary offset",
)
old_release = '''        if (temporary_bytes != 0U) {
            if (fprintf(file, "  addi sp, sp, %zu\\n", temporary_bytes) < 0) {
                return false;
            }
        }
        if (is_indirect) {
            if (fprintf(file, "  jalr ra, t0, 0\\n") < 0) {
                return false;
            }
        } else if (fprintf(file, "  call %s\\n", direct_callee->name) < 0) {
            return false;
        }
'''
new_release = '''        if (outgoing_stack_bytes == 0U && temporary_bytes != 0U &&
            !minic_riscv64_emit_stack_release(file, temporary_bytes)) {
            return false;
        }
        if (is_indirect) {
            if (fprintf(file, "  jalr ra, t0, 0\\n") < 0) {
                return false;
            }
        } else if (fprintf(file, "  call %s\\n", direct_callee->name) < 0) {
            return false;
        }
        if (outgoing_stack_bytes != 0U) {
            if (temporary_bytes > SIZE_MAX - outgoing_stack_bytes ||
                !minic_riscv64_emit_stack_release(
                    file, temporary_bytes + outgoing_stack_bytes)) {
                return false;
            }
        }
'''
text = replace_exact(text, old_release, new_release, "caller temporary/outgoing stack lifetime")
path.write_text(text)

print("staged up to 16 fixed parameters with RV64 integer-class stack arguments")
