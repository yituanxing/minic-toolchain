#!/usr/bin/env python3
from pathlib import Path

path = Path('src/target/riscv64/codegen_function.c')
text = path.read_text()

old = '''        success =
            minic_riscv64_emit_global_object(file, program, &program->global_objects[global_index]);
'''
new = '''        success =
            minic_riscv64_emit_global_object(file, program, &program->global_objects[global_index]);
        if (!success && diagnostic != NULL && diagnostic->message[0] == '\\0') {
            char message[256];
            const MinicGlobalObject *object;

            object = &program->global_objects[global_index];
            (void)snprintf(message,
                           sizeof(message),
                           "cannot emit RISC-V global object '%s'",
                           object->name_length != 0U ? object->name : "<anonymous>");
            minic_riscv64_set_diagnostic(diagnostic, path, message);
        }
'''
if text.count(old) == 1:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit('global emission anchor changed')

old = '''        if (core_function != NULL &&
            minic_riscv64_core_function_can_emit_basic_v0_for_program(program, core_function)) {
            MinicRiscv64FunctionSymbol symbol;

            success = minic_riscv64_function_symbol_from_function(function, &symbol) &&
                      minic_riscv64_emit_core_function_basic_v0_for_program_with_symbol(
                          file, program, core_function, &symbol);
        } else {
            success = minic_riscv64_emit_function(file, program, function, &label_counter);
        }
'''
new = '''        if (core_function != NULL &&
            minic_riscv64_core_function_can_emit_basic_v0_for_program(program, core_function)) {
            MinicRiscv64FunctionSymbol symbol;

            success = minic_riscv64_function_symbol_from_function(function, &symbol) &&
                      minic_riscv64_emit_core_function_basic_v0_for_program_with_symbol(
                          file, program, core_function, &symbol);
        } else {
            success = minic_riscv64_emit_function(file, program, function, &label_counter);
        }
        if (!success && diagnostic != NULL && diagnostic->message[0] == '\\0') {
            char message[256];
            const char *symbol_name;

            symbol_name = minic_c0_function_symbol_name(function);
            (void)snprintf(message,
                           sizeof(message),
                           "cannot emit RISC-V function '%s'",
                           symbol_name != NULL && symbol_name[0] != '\\0' ? symbol_name
                                                                         : "<anonymous>");
            minic_riscv64_set_diagnostic(diagnostic, path, message);
        }
'''
if text.count(old) == 1:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit('function emission anchor changed')

old = '''    if (!success) {
        minic_riscv64_set_diagnostic(diagnostic, path, "cannot write RISC-V assembly");
    }
'''
new = '''    if (!success && (diagnostic == NULL || diagnostic->message[0] == '\\0')) {
        minic_riscv64_set_diagnostic(diagnostic, path, "cannot write RISC-V assembly");
    }
'''
if text.count(old) == 1:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit('generic diagnostic anchor changed')

path.write_text(text)
