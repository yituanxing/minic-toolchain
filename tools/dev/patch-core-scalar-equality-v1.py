from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected exactly one anchor in {path}, found {count}")
    file.write_text(text.replace(old, new, 1))


def replace_symbol(path: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count("MINIC_CORE_INSTRUCTION_INTEGER_EQUAL")
    if count == 0:
        raise SystemExit(f"expected equality instruction symbol in {path}")
    file.write_text(text.replace("MINIC_CORE_INSTRUCTION_INTEGER_EQUAL",
                                 "MINIC_CORE_INSTRUCTION_SCALAR_EQUAL"))


# Pointer equality is now a second real consumer of the same target-neutral equality fact.
# Rename the Core operation rather than letting an integer-named instruction accept pointers.
for source in (
    "src/core/core_ir.h",
    "src/core/core_ir.c",
    "src/core/core_lower.c",
    "src/target/riscv64/core_codegen.c",
):
    replace_symbol(source)

replace_once(
    "src/core/core_ir.c",
    "        return minic_type_is_integer(left->type) && minic_type_equal(left->type, right->type);",
    "        return (minic_type_is_integer(left->type) || minic_type_is_pointer(left->type)) &&\n"
    "               minic_type_equal(left->type, right->type);",
)
replace_once(
    "src/core/core_ir.c",
    '                       "  %%%" PRIu32 " = eq.int %%%" PRIu32 ", %%%" PRIu32 "\\n",',
    '                       "  %%%" PRIu32 " = eq.scalar %%%" PRIu32 ", %%%" PRIu32 "\\n",',
)
replace_once(
    "src/core/core_lower.c",
    "            !minic_type_is_integer(context->function->values[left].type) ||\n"
    "            !minic_type_equal(context->function->values[left].type,\n"
    "                              context->function->values[right].type)) {",
    "            (!minic_type_is_integer(context->function->values[left].type) &&\n"
    "             !minic_type_is_pointer(context->function->values[left].type)) ||\n"
    "            !minic_type_equal(context->function->values[left].type,\n"
    "                              context->function->values[right].type)) {",
)

# Extend the existing equality differential with pointer and record-member pointer equality.
path = Path("tests/compiler/c0/core_integer_equality.c")
text = path.read_text()
text += """

struct core_m11_node {
    struct core_m11_node *next;
    struct core_m11_node *prev;
};

int core_m11_pointer_equal(int *left, int *right) {
    return left == right;
}

int core_m11_member_pointer_equal(struct core_m11_node *node,
                                  struct core_m11_node *expected) {
    return node->next == expected;
}
"""
path.write_text(text)

path = Path("tests/compiler/c0/core_integer_equality_runtime.c")
text = path.read_text()
replace_once(
    "tests/compiler/c0/core_integer_equality_runtime.c",
    "void core_m5b_set_if_equal(int value, int expected);\n",
    "void core_m5b_set_if_equal(int value, int expected);\n\n"
    "struct core_m11_node {\n"
    "    struct core_m11_node *next;\n"
    "    struct core_m11_node *prev;\n"
    "};\n\n"
    "int core_m11_pointer_equal(int *left, int *right);\n"
    "int core_m11_member_pointer_equal(struct core_m11_node *node,\n"
    "                                  struct core_m11_node *expected);\n",
)
replace_once(
    "tests/compiler/c0/core_integer_equality_runtime.c",
    "    int before_unequal;\n\n"
    "    before_equal = core_m5b_equal(13);",
    "    int before_unequal;\n"
    "    int left_value;\n"
    "    int right_value;\n"
    "    struct core_m11_node node;\n"
    "    struct core_m11_node other;\n\n"
    "    before_equal = core_m5b_equal(13);",
)
replace_once(
    "tests/compiler/c0/core_integer_equality_runtime.c",
    "    core_m5b_set_if_equal(29, 13);\n"
    "    (void)printf(\"%d %d %d\\n\", before_equal, before_unequal, core_m5b_global);",
    "    core_m5b_set_if_equal(29, 13);\n"
    "    left_value = 1;\n"
    "    right_value = 2;\n"
    "    node.next = &other;\n"
    "    node.prev = &node;\n"
    "    other.next = &node;\n"
    "    other.prev = &other;\n"
    "    (void)printf(\"%d %d %d %d %d %d\\n\",\n"
    "                 before_equal,\n"
    "                 before_unequal,\n"
    "                 core_m5b_global,\n"
    "                 core_m11_pointer_equal(&left_value, &left_value),\n"
    "                 core_m11_pointer_equal(&left_value, &right_value),\n"
    "                 core_m11_member_pointer_equal(&node, &other));",
)

replace_once(
    "tests/compiler/c0/run-core-integer-equality.sh",
    "grep -q '^core_m5b_set_if_equal:' \"$work/core_integer_equality-core.s\"\n",
    "grep -q '^core_m5b_set_if_equal:' \"$work/core_integer_equality-core.s\"\n"
    "grep -q '^core_m11_pointer_equal:' \"$work/core_integer_equality-core.s\"\n"
    "grep -q '^core_m11_member_pointer_equal:' \"$work/core_integer_equality-core.s\"\n",
)
