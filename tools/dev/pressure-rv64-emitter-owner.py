from pathlib import Path

function_path = Path("src/target/riscv64/codegen_function.c")
text = function_path.read_text()

global_old = """        success =
            minic_riscv64_emit_global_object(file, program, &program->global_objects[global_index]);
        if (!success) {
        }
"""
global_new = """        success =
            minic_riscv64_emit_global_object(file, program, &program->global_objects[global_index]);
        if (!success) {
            fprintf(stderr,
                    "MINIC_RV64_EMIT_FAIL kind=global index=%zu name=%s\\n",
                    global_index,
                    program->global_objects[global_index].name);
        }
"""
function_old = """        success = minic_riscv64_emit_function(file, program, function, &label_counter);
        if (!success) {
        }
"""
function_new = """        success = minic_riscv64_emit_function(file, program, function, &label_counter);
        if (!success) {
            fprintf(stderr,
                    "MINIC_RV64_EMIT_FAIL kind=function index=%zu name=%s\\n",
                    function_index,
                    minic_c0_function_symbol_name(function));
        }
"""
if text.count(global_old) != 1 or text.count(function_old) != 1:
    raise SystemExit("RV64 writer instrumentation anchors changed")
function_path.write_text(text.replace(global_old, global_new, 1).replace(function_old, function_new, 1))

statement_path = Path("src/target/riscv64/codegen_statement.c")
text = statement_path.read_text()
old = """        if (!minic_riscv64_emit_statement(file,
                                          program,
                                          function,
                                          function_layout,
                                          statement_id,
                                          statement,
                                          label_counter,
                                          break_target)) {
            return false;
        }
"""
new = """        if (!minic_riscv64_emit_statement(file,
                                          program,
                                          function,
                                          function_layout,
                                          statement_id,
                                          statement,
                                          label_counter,
                                          break_target)) {
            fprintf(stderr,
                    "MINIC_RV64_STATEMENT_FAIL function=%s block=%zu index=%zu statement=%zu statement_kind=%d\\n",
                    minic_c0_function_symbol_name(function),
                    (size_t)block_id,
                    index,
                    (size_t)statement_id,
                    statement != NULL ? (int)statement->kind : -1);
            return false;
        }
"""
if text.count(old) != 1:
    raise SystemExit("RV64 statement instrumentation anchor changed")
statement_path.write_text(text.replace(old, new, 1))
