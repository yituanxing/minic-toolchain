#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    if old not in text:
        raise SystemExit(f"missing expected text in {path}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/target/riscv64/codegen_expression.c",
    """    case MINIC_EXPRESSION_GLOBAL_OBJECT:
        return false;
""",
    """    case MINIC_EXPRESSION_GLOBAL_OBJECT: {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(program, expression->value.global_object_id);
        if (object == NULL || object->name_length == 0U || minic_type_is_array(object->type) ||
            minic_type_is_record(object->type) ||
            fprintf(file, "  la a0, %s\\n", object->name) < 0) {
            return false;
        }
        return minic_riscv64_emit_scalar_load(file, object->type, "a0", "a0");
    }
""",
)

print("staged global scalar RV64 loads")
