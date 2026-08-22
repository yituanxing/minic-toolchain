#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file_path = Path(path)
    text = file_path.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"expected exactly one anchor in {path}: {old[:100]!r}")
    file_path.write_text(text.replace(old, new, 1))


replace_once(
    "src/core/core_lower.c",
    "        if (!minic_type_is_integer(context->source_function->return_type)) {\n"
    "            return MINIC_CORE_LOWER_UNSUPPORTED;\n"
    "        }\n"
    "        status = lower_integer_assignment_value(context,\n"
    "                                                context->source_function->return_type,\n"
    "                                                statement->expression,\n"
    "                                                &terminator.return_value);\n"
    "        if (status != MINIC_CORE_LOWER_OK) {\n"
    "            return status;\n"
    "        }\n",
    "        if (minic_type_is_integer(context->source_function->return_type)) {\n"
    "            status = lower_integer_assignment_value(context,\n"
    "                                                    context->source_function->return_type,\n"
    "                                                    statement->expression,\n"
    "                                                    &terminator.return_value);\n"
    "        } else if (minic_type_is_pointer(context->source_function->return_type)) {\n"
    "            status = lower_expression(context, statement->expression, &terminator.return_value);\n"
    "            if (status == MINIC_CORE_LOWER_OK &&\n"
    "                (terminator.return_value >= context->function->value_count ||\n"
    "                 !minic_type_equal(context->function->values[terminator.return_value].type,\n"
    "                                   context->source_function->return_type))) {\n"
    "                return MINIC_CORE_LOWER_UNSUPPORTED;\n"
    "            }\n"
    "        } else {\n"
    "            return MINIC_CORE_LOWER_UNSUPPORTED;\n"
    "        }\n"
    "        if (status != MINIC_CORE_LOWER_OK) {\n"
    "            return status;\n"
    "        }\n",
)

replace_once(
    "tests/target/riscv64/core_hybrid_differential.i",
    "int core_hybrid_fallback_load(int *value) {\n"
    "    return *value;\n"
    "}\n",
    "int core_hybrid_fallback_load(int *value) {\n"
    "    return *value;\n"
    "}\n\n"
    "int core_hybrid_indirect_target(int value) {\n"
    "    return value + 4;\n"
    "}\n\n"
    "int core_hybrid_fallback_indirect(int (*callee)(int), int value) {\n"
    "    return callee(value);\n"
    "}\n",
)

replace_once(
    "tests/target/riscv64/core_hybrid_differential_runtime.c",
    "int core_hybrid_fallback_load(int *value);\n",
    "int core_hybrid_fallback_load(int *value);\n"
    "int core_hybrid_indirect_target(int value);\n"
    "int core_hybrid_fallback_indirect(int (*callee)(int), int value);\n",
)

replace_once(
    "tests/target/riscv64/core_hybrid_differential_runtime.c",
    "    (void)printf(\"%d %d %d %d %d\\n\",\n"
    "                 core_hybrid_core(5),\n"
    "                 core_hybrid_call(10),\n"
    "                 core_hybrid_field(&layout),\n"
    "                 layout.value,\n"
    "                 core_hybrid_fallback_load(&value));\n",
    "    (void)printf(\"%d %d %d %d %d %d\\n\",\n"
    "                 core_hybrid_core(5),\n"
    "                 core_hybrid_call(10),\n"
    "                 core_hybrid_field(&layout),\n"
    "                 layout.value,\n"
    "                 core_hybrid_fallback_load(&value),\n"
    "                 core_hybrid_fallback_indirect(core_hybrid_indirect_target, 20));\n",
)

script = Path("tests/compiler/c0/run-core-scalar-lvalue-bitcast.sh")
text = script.read_text()
old = "\ngrep -q 'bitcast.scalar' <(MINIC_CORE_IR=strict \"$MINIC\" -S \\\n    \"$work/core_scalar_lvalue_bitcast.i\" -o /dev/null 2>&1 || true) || true\n"
if old not in text:
    raise SystemExit("bitcast regression cleanup anchor not found")
script.write_text(text.replace(old, "", 1))
