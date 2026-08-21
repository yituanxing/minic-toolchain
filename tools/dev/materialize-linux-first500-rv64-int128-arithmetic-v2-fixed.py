#!/usr/bin/env python3
"""Execute the int128 arithmetic materializer with its definition anchor disambiguated."""
from pathlib import Path

source = Path("tools/dev/materialize-linux-first500-rv64-int128-arithmetic-v2.py").read_text()
short_old = "'''static bool minic_riscv64_emit_expression_impl(FILE *file,\\n''',"
full_old = "'''static bool minic_riscv64_emit_expression_impl(FILE *file,\\n                                               const MinicC0Program *program,\\n                                               const MinicFunction *function,\\n                                               const MinicRiscv64FunctionLayout *function_layout,\\n                                               MinicExpressionId expression_id,\\n                                               size_t record_result_temporary_size) {\\n''',"
short_new_end = "static bool minic_riscv64_emit_expression_impl(FILE *file,\\n''')"
full_new_end = "static bool minic_riscv64_emit_expression_impl(FILE *file,\\n                                               const MinicC0Program *program,\\n                                               const MinicFunction *function,\\n                                               const MinicRiscv64FunctionLayout *function_layout,\\n                                               MinicExpressionId expression_id,\\n                                               size_t record_result_temporary_size) {\\n''')"
if source.count(short_old) != 1 or source.count(short_new_end) != 1:
    raise SystemExit("unexpected int128 materializer anchor shape")
source = source.replace(short_old, full_old, 1).replace(short_new_end, full_new_end, 1)
namespace = {"__name__": "__main__", "__file__": "tools/dev/materialize-linux-first500-rv64-int128-arithmetic-v2.py"}
exec(compile(source, namespace["__file__"], "exec"), namespace)
