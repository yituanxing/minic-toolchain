#!/usr/bin/env python3
"""Materialize generic RV64 statement-owner diagnostics for emission failures."""
from pathlib import Path

path = Path("src/target/riscv64/codegen_statement.c")
text = path.read_text()
marker = "RV64_EMIT_STATEMENT_FAILURE"
if marker not in text:
    old = '''        if (!minic_riscv64_emit_statement(file,
                                          program,
                                          function,
                                          function_layout,
                                          statement_id,
                                          statement,
                                          label_counter,
                                          break_target)) {
            return false;
        }
'''
    new = '''        if (!minic_riscv64_emit_statement(file,
                                          program,
                                          function,
                                          function_layout,
                                          statement_id,
                                          statement,
                                          label_counter,
                                          break_target)) {
            (void)fprintf(stderr,
                          "RV64_EMIT_STATEMENT_FAILURE block=%zu ordinal=%zu statement=%zu "
                          "kind=%u expression=%zu target_expression=%zu\\n",
                          (size_t)block_id,
                          index,
                          (size_t)statement_id,
                          statement != NULL ? (unsigned int)statement->kind : UINT_MAX,
                          statement != NULL ? (size_t)statement->expression : SIZE_MAX,
                          statement != NULL ? (size_t)statement->target_expression : SIZE_MAX);
            return false;
        }
'''
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"statement owner anchor: expected 1 match, found {count}")
    # UINT_MAX is already available transitively in this TU's current build, but use an
    # enum-independent sentinel without adding another include.
    new = new.replace("UINT_MAX", "(unsigned int)-1")
    path.write_text(text.replace(old, new, 1))
