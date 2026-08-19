#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
V1 = ROOT / "tools/dev/materialize-first500-pareto-v1.py"
source = V1.read_text()

old_func = '''def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one occurrence, found {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))
'''

new_func = r'''def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count == 1:
        write(path, text.replace(old, new, 1))
        return

    # Current post-ownership parser spelling: null handling uses the shared
    # expression_is_integer_zero helper. Keep that spelling while adding the
    # same-record identity-cast rule.
    if path == "src/frontend/parser_expression.c" and "unsupported cast between these types" in old:
        current = """    if (operand == NULL ||
        (!minic_type_is_void(target_type) &&
         !minic_type_cast_compatible(target_type, operand->type) &&
         !(minic_type_is_pointer(target_type) && expression_is_integer_zero(operand)))) {
        minic_parser_error(parser, \"unsupported cast between these types\");
        return false;
    }
"""
        replacement = """    if (operand == NULL ||
        (!minic_type_is_void(target_type) &&
         !minic_type_cast_compatible(target_type, operand->type) &&
         !(minic_type_is_record(target_type) && minic_type_is_record(operand->type) &&
           minic_c0_types_compatible(parser->program, target_type, operand->type)) &&
         !(minic_type_is_pointer(target_type) && expression_is_integer_zero(operand)))) {
        minic_parser_error(parser, \"unsupported cast between these types\");
        return false;
    }
"""
        if text.count(current) != 1:
            raise SystemExit(f"{path}: current identity-cast anchor not unique")
        write(path, text.replace(current, replacement, 1))
        return

    # Current record parser additionally checks the descriptor semantic class
    # before applying packed. Preserve that ownership check and admit only the
    # explicit diagnostic-only nonstring attribute alongside it.
    if path == "src/frontend/parser_record.c" and "unsupported GNU record field attribute" in old:
        current = """    if (descriptor->kind == MINIC_ATTRIBUTE_PACKED &&
        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_LAYOUT) {
        context->is_packed = true;
        return true;
    }
    minic_parser_error(parser, \"unsupported GNU record field attribute\");
"""
        replacement = """    if (descriptor->kind == MINIC_ATTRIBUTE_PACKED &&
        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_LAYOUT) {
        context->is_packed = true;
        return true;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_NONSTRING &&
        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC) {
        return true;
    }
    minic_parser_error(parser, \"unsupported GNU record field attribute\");
"""
        if text.count(current) != 1:
            raise SystemExit(f"{path}: current record-field attribute anchor not unique")
        write(path, text.replace(current, replacement, 1))
        return

    raise SystemExit(f"{path}: expected one occurrence, found {count}: {old[:120]!r}")
'''

if source.count(old_func) != 1:
    raise SystemExit("v1 replace_once definition changed")
source = source.replace(old_func, new_func, 1)

# The nonstring capability is declaration semantics only. Keep its focused
# fixture independent from the separate runtime record-array initializer
# capability so a failure here diagnoses the attribute mechanism itself.
old_fixture = '''    struct ext4_like_super_block value = { .s_last_mounted = "x" };
    return value.s_last_mounted[0] == 'x' ? 0 : 1;'''
new_fixture = '''    struct ext4_like_super_block value;
    value.s_last_mounted[0] = 'x';
    return value.s_last_mounted[0] == 'x' ? 0 : 1;'''
if source.count(old_fixture) != 1:
    raise SystemExit("v1 nonstring fixture changed")
source = source.replace(old_fixture, new_fixture, 1)

namespace = {"__file__": str(V1), "__name__": "__main__"}
exec(compile(source, str(V1), "exec"), namespace)
