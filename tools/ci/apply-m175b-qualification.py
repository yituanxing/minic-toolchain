#!/usr/bin/env python3
from pathlib import Path
import subprocess


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


# Keep the staging helper compatible with the current core_codegen support switch.
helper = Path("tools/ci/apply-m175b-scalar-double.py")
text = helper.read_text()
old = '''text = replace_once(
    text,
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:\\n",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:\\n"
    "    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:\\n",
    "core-codegen support floating constant",
)'''
new = '''text = replace_nth(
    text,
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:\\n",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:\\n"
    "    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:\\n",
    1,
    "core-codegen support floating constant",
)'''
if old in text:
    helper.write_text(text.replace(old, new, 1))
elif new not in text:
    raise SystemExit("M175B qualification: support-switch correction anchor missing")

subprocess.run(["python3", str(helper)], check=True)

# Close the two shared scalar-owner seams exposed by the first M175B runs.
path = Path("src/core/core_lower.c")
text = path.read_text()
if "M175B_SCALAR_RETURN_OWNER" not in text:
    assignment_function = text.find(
        "static MinicCoreLowerStatus lower_scalar_assignment_value("
    )
    assignment_bound = text.find(
        "    if (source_value >= context->function->value_count) {\n",
        assignment_function,
    )
    pointer_start = text.find(
        "    if (minic_type_is_pointer(target_type)) {\n",
        assignment_bound,
    )
    if assignment_function < 0 or assignment_bound < 0 or pointer_start < 0:
        raise SystemExit("M175B qualification: scalar assignment owner anchor missing")
    double_identity = (
        "    if (minic_type_is_double(target_type)) {\n"
        "        MinicType source_type;\n"
        "\n"
        "        if (!core_scalar_expression_value_type(context->body, expression, &source_type) ||\n"
        "            !minic_type_is_double(source_type) ||\n"
        "            !minic_type_equal(source_type, target_type)) {\n"
        "            return MINIC_CORE_LOWER_UNSUPPORTED;\n"
        "        }\n"
        "        if (!minic_type_equal(context->function->values[source_value].type, source_type)) {\n"
        "            return MINIC_CORE_LOWER_ERROR;\n"
        "        }\n"
        "        *value_id = source_value;\n"
        "        return MINIC_CORE_LOWER_OK;\n"
        "    }\n"
    )
    text = text[:pointer_start] + double_identity + text[pointer_start:]

    return_function = text.find("static MinicCoreLowerStatus lower_return(")
    return_start = text.find(
        "        if (minic_type_is_integer(context->source_function->return_type)) {\n",
        return_function,
    )
    record_start = text.find(
        "        } else if (minic_type_is_record(context->source_function->return_type)) {\n",
        return_start,
    )
    if return_function < 0 or return_start < 0 or record_start < 0:
        raise SystemExit("M175B qualification: scalar return owner anchor missing")
    new_return = (
        "        if (core_memory_scalar_type(context->source_function->return_type)) {\n"
        "            /* M175B_SCALAR_RETURN_OWNER: all Core memory scalars use the same\n"
        "               C assignment/value transport at the return boundary. */\n"
        "            status = lower_scalar_assignment_value(context,\n"
        "                                                   context->source_function->return_type,\n"
        "                                                   statement->expression,\n"
        "                                                   &terminator.return_value);\n"
    )
    text = text[:return_start] + new_return + text[record_start:]
    path.write_text(text)

# The current full-fast blocker is a declaration/Sema hole, not a double issue.
# Stage its semantic owner fix so Linux500 can be qualified before unrelated fast debt.
subprocess.run(["python3", "tools/ci/apply-m175c-local-complete-object.py"], check=True)

print("staged M175B qualification candidate with local complete-object owner")
