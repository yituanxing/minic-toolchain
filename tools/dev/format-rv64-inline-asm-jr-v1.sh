#!/bin/sh
set -eu
clang-format -i src/target/riscv64/codegen_inline_asm.c tests/compiler/c0/gnu_inline_asm_operands.c
clang-format --dry-run --Werror src/target/riscv64/codegen_inline_asm.c tests/compiler/c0/gnu_inline_asm_operands.c
