#!/usr/bin/env python3
from pathlib import Path

path = Path("Makefile")
text = path.read_text()
production = "\tsrc/frontend/ast.c \\\n\tsrc/frontend/ast_verifier.c \\\n"
production_replacement = (
    "\tsrc/frontend/ast.c \\\n"
    "\tsrc/frontend/ast_traversal.c \\\n"
    "\tsrc/frontend/function_body.c \\\n"
    "\tsrc/frontend/ast_verifier.c \\\n"
)
contract = (
    "AST_CONTRACT_TEST_SOURCES := \\\n"
    "\tsrc/frontend/ast.c \\\n"
    "\tsrc/frontend/ast_global.c \\\n"
    "\tsrc/frontend/ast_verifier.c \\\n"
)
contract_replacement = (
    "AST_CONTRACT_TEST_SOURCES := \\\n"
    "\tsrc/frontend/ast.c \\\n"
    "\tsrc/frontend/ast_global.c \\\n"
    "\tsrc/frontend/ast_traversal.c \\\n"
    "\tsrc/frontend/ast_verifier.c \\\n"
)
if text.count(production) != 1 or text.count(contract) != 1:
    raise SystemExit("unexpected Makefile source-list shape")
text = text.replace(production, production_replacement, 1)
text = text.replace(contract, contract_replacement, 1)
path.write_text(text)
