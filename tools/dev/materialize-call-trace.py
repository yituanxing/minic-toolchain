#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_expression.c")
text = path.read_text()

old = r'''            if (!minic_riscv64_emit_expression(
                    file, program, function, expression->value.call.arguments[argument_index])) {
                return false;
            }
'''
new = r'''            if (!minic_riscv64_emit_expression(
                    file, program, function, expression->value.call.arguments[argument_index])) {
                fprintf(stderr,
                        "CODEGEN_CALL_ARG scalar caller=%s callee=%s arg=%zu expr=%zu kind=%d type=%d/%u vcat=%d fixed=%d abi_type=%d/%u\n",
                        function != NULL ? function->name : "<null>",
                        direct_callee != NULL ? direct_callee->name : "<indirect>",
                        argument_index,
                        (size_t)expression->value.call.arguments[argument_index],
                        (int)argument->kind,
                        (int)argument->type.base_kind,
                        argument->type.pointer_depth,
                        (int)argument->value_category,
                        argument_index < parameter_count ? 1 : 0,
                        argument_index < parameter_count ? (int)abi_parameter_types[argument_index].base_kind : -1,
                        argument_index < parameter_count ? abi_parameter_types[argument_index].pointer_depth : 0U);
                return false;
            }
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("scalar call argument trace anchor not found")

path.write_text(text)
