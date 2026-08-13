#!/usr/bin/env python3
from pathlib import Path

path = Path("Makefile")
text = path.read_text()
needle = "\tsrc/frontend/ast.c \\\n\tsrc/frontend/ast_verifier.c \\\n"
replacement = (
    "\tsrc/frontend/ast.c \\\n"
    "\tsrc/frontend/ast_traversal.c \\\n"
    "\tsrc/frontend/function_body.c \\\n"
    "\tsrc/frontend/ast_verifier.c \\\n"
)
if text.count(needle) != 1:
    raise SystemExit("unexpected production source-list shape")
path.write_text(text.replace(needle, replacement, 1))
