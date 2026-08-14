#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_expression.c")
source = path.read_text()

old = """            if (value->kind == MINIC_RISCV64_ABI_VALUE_IGNORE ||\n                value->kind == MINIC_RISCV64_ABI_VALUE_INDIRECT ||\n                (value->kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE &&\n                 value->storage_size != 8U && value->storage_size != 16U)) {\n"""
new = """            if (value->kind == MINIC_RISCV64_ABI_VALUE_IGNORE ||\n                value->kind == MINIC_RISCV64_ABI_VALUE_INDIRECT ||\n                (value->kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE &&\n                 (argument_index >= parameter_count ||\n                  (value->storage_size != 8U && value->storage_size != 16U)))) {\n"""
if source.count(old) != 1:
    raise SystemExit("formal-v1 hybrid selector anchor missing/non-unique")
path.write_text(source.replace(old, new, 1))
print("MATERIALIZED rv64-caller-abi-hybrid-formal-boundary")
