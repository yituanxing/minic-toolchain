#!/usr/bin/env python3
from pathlib import Path

source = Path("tools/ci/apply-m158-final-four.py").read_text()
old = '''def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)
'''
new = '''def replace_once(text: str, old: str, new: str, label: str) -> str:
    if label == "ctzl lowering":
        marker = "M129_LEAF_EXPRESSION_OWNERS"
        marker_at = text.find(marker)
        if marker_at < 0:
            raise SystemExit("ctzl lowering: M129 owner marker missing")
        anchor_at = text.find(old, marker_at)
        if anchor_at < 0:
            raise SystemExit("ctzl lowering: owner-local call anchor missing")
        return text[:anchor_at] + new + text[anchor_at + len(old):]
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)
'''
if source.count(old) != 1:
    raise SystemExit("M158 v2 could not patch replace_once helper")
source = source.replace(old, new, 1)

# Frontend parse_goto stores GNU `goto *expr` in statement.expression.  The
# target_expression field is unrelated and remains INVALID for this statement.
source = source.replace(
    '''                if (statement->target_expression != MINIC_EXPRESSION_INVALID) {\n                    MinicCoreTerminator terminator;\n                    MinicCoreValueId target_value;\n\n                    if (statement->expression != MINIC_EXPRESSION_INVALID ||\n                        statement->target_statement != MINIC_STATEMENT_INVALID) {\n                        status = MINIC_CORE_LOWER_UNSUPPORTED;\n                        break;\n                    }\n                    status = lower_expression(\n                        context, statement->target_expression, &target_value);''',
    '''                if (statement->expression != MINIC_EXPRESSION_INVALID &&\n                    statement->target_statement == MINIC_STATEMENT_INVALID) {\n                    MinicCoreTerminator terminator;\n                    MinicCoreValueId target_value;\n\n                    if (statement->target_expression != MINIC_EXPRESSION_INVALID) {\n                        status = MINIC_CORE_LOWER_UNSUPPORTED;\n                        break;\n                    }\n                    status = lower_expression(\n                        context, statement->expression, &target_value);''',
)

# Diagnostics must not depend on MinicType's private representation.  We only
# need enough information to distinguish qualifier/array/register rejection,
# local-object rejection, and post-object non-scalar rejection.
source = source.replace(
    '''                          "volatile=%d array=%d register=%d raw_kind=%d signature_kind=%d\\\\n",\n                          context->source_function->name,\n                          parameter_index,\n                          minic_type_is_volatile(parameter->type) ? 1 : 0,\n                          parameter->is_array ? 1 : 0,\n                          parameter->is_register_storage ? 1 : 0,\n                          (int)parameter->type.kind,\n                          (int)context->source_function->parameter_types[parameter_index].kind);''',
    '''                          "volatile=%d array=%d register=%d\\\\n",\n                          context->source_function->name,\n                          parameter_index,\n                          minic_type_is_volatile(parameter->type) ? 1 : 0,\n                          parameter->is_array ? 1 : 0,\n                          parameter->is_register_storage ? 1 : 0);''',
)
source = source.replace(
    '''                          "CORE_M158_INGRESS_DETAIL function=%s parameter=%zu nonscalar_kind=%d\\\\n",\n                          context->source_function->name,\n                          parameter_index,\n                          (int)parameter_value_type.kind);''',
    '''                          "CORE_M158_INGRESS_DETAIL function=%s parameter=%zu nonscalar=1\\\\n",\n                          context->source_function->name,\n                          parameter_index);''',
)
exec(compile(source, "tools/ci/apply-m158-final-four.py", "exec"), {"__name__": "__main__"})
