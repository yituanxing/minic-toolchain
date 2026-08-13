#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_statement.c")
text = path.read_text()
old = r'''            fprintf(stderr,
                    "CODEGEN_FAIL statement function=%s block=%zu statement=%zu kind=%d\n",
                    function != NULL ? function->name : "<null>",
                    (size_t)block_id,
                    (size_t)statement_id,
                    statement != NULL ? (int)statement->kind : -1);
'''
new = r'''            fprintf(stderr,
                    "CODEGEN_FAIL statement function=%s block=%zu statement=%zu kind=%d line=%zu column=%zu\n",
                    function != NULL ? function->name : "<null>",
                    (size_t)block_id,
                    (size_t)statement_id,
                    statement != NULL ? (int)statement->kind : -1,
                    statement != NULL ? statement->span.begin.line : 0U,
                    statement != NULL ? statement->span.begin.column : 0U);
'''
if old not in text:
    if new in text:
        raise SystemExit(0)
    raise SystemExit("statement codegen trace anchor not found")
path.write_text(text.replace(old, new, 1))
