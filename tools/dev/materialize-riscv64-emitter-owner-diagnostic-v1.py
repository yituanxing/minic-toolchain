#!/usr/bin/env python3
"""Materialize precise RV64 emission owner diagnostics."""
from pathlib import Path

path = Path("src/target/riscv64/codegen_function.c")
text = path.read_text()

before_global = '''        success =
            minic_riscv64_emit_global_object(file, program, &program->global_objects[global_index]);
'''
after_global = '''        if (!minic_riscv64_emit_global_object(
                file, program, &program->global_objects[global_index])) {
            char message[256];
            const MinicGlobalObject *object;

            object = &program->global_objects[global_index];
            (void)snprintf(message,
                           sizeof(message),
                           "cannot emit RISC-V global object '%s' (index=%zu)",
                           object->name,
                           global_index);
            minic_riscv64_set_diagnostic(diagnostic, path, message);
            success = false;
        }
'''
if after_global not in text:
    if text.count(before_global) != 1:
        raise SystemExit("global emitter diagnostic anchor not found uniquely")
    text = text.replace(before_global, after_global, 1)

before_function = '''        } else {
            success = minic_riscv64_emit_function(file, program, function, &label_counter);
        }
    }

    if (!success) {
        minic_riscv64_set_diagnostic(diagnostic, path, "cannot write RISC-V assembly");
    }
'''
after_function = '''        } else {
            success = minic_riscv64_emit_function(file, program, function, &label_counter);
        }
        if (!success && diagnostic != NULL && diagnostic->message[0] == '\\0') {
            char message[256];
            const char *symbol_name;

            symbol_name = minic_c0_function_symbol_name(function);
            (void)snprintf(message,
                           sizeof(message),
                           "cannot emit RISC-V function '%s' (index=%zu)",
                           symbol_name != NULL ? symbol_name : "<unnamed>",
                           function_index);
            minic_riscv64_set_diagnostic(diagnostic, path, message);
        }
    }

    if (!success && (diagnostic == NULL || diagnostic->message[0] == '\\0')) {
        minic_riscv64_set_diagnostic(diagnostic, path, "cannot write RISC-V assembly");
    }
'''
if after_function not in text:
    if text.count(before_function) != 1:
        raise SystemExit("function emitter diagnostic anchor not found uniquely")
    text = text.replace(before_function, after_function, 1)

path.write_text(text)
