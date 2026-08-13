#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_expression.c")
text = path.read_text()
old = '''                    if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                        const char *destination_register;

                        destination_register = integer_register_index < 8U ?
                                                   minic_riscv64_argument_registers[integer_register_index] :
                                                   "t0";
                        if (offset <= 2047U) {
                            if (fprintf(file,
                                        "  addi %s, sp, %zu\\n",
                                        destination_register,
                                        offset) < 0) {
                                return false;
                            }
                        } else if (fprintf(file,
                                           "  li t1, %zu\\n  add %s, sp, t1\\n",
                                           offset,
                                           destination_register) < 0) {
                            return false;
                        }
                        if (integer_register_index < 8U) {
                            integer_register_index += 1U;
                        } else {
                            if (!minic_riscv64_emit_sp_store64(
                                    file, "t0", stack_argument_index * 8U)) {
                                return false;
                            }
                            stack_argument_index += 1U;
                        }
'''
new = '''                    if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                        if (integer_register_index < 8U) {
                            if (offset <= 2047U) {
                                if (fprintf(file,
                                            "  addi a%zu, sp, %zu\\n",
                                            integer_register_index,
                                            offset) < 0) {
                                    return false;
                                }
                            } else if (fprintf(file,
                                               "  li t1, %zu\\n  add a%zu, sp, t1\\n",
                                               offset,
                                               integer_register_index) < 0) {
                                return false;
                            }
                            integer_register_index += 1U;
                        } else {
                            if (offset <= 2047U) {
                                if (fprintf(file, "  addi t0, sp, %zu\\n", offset) < 0) {
                                    return false;
                                }
                            } else if (fprintf(file,
                                               "  li t1, %zu\\n  add t0, sp, t1\\n",
                                               offset) < 0) {
                                return false;
                            }
                            if (!minic_riscv64_emit_sp_store64(
                                    file, "t0", stack_argument_index * 8U)) {
                                return false;
                            }
                            stack_argument_index += 1U;
                        }
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("indirect aggregate register materialization anchor not found")
path.write_text(text)
