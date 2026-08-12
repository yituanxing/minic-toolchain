from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"anchor mismatch {path}: {count}")
    p.write_text(text.replace(old, new, 1))


# Make the existing expression-level null semantic explicit and bounded to actual
# C null pointer constants supported by v0: integer literal zero and that literal
# cast to unqualified void *. Do not treat arbitrary typed pointer casts as NPCs.
replace_once(
    "src/frontend/ast.h",
    "bool minic_c0_assignment_compatible(const MinicC0Program *program,\n                                    MinicType target_type,\n                                    MinicExpressionId source_expression_id);",
    "bool minic_c0_expression_is_null_pointer_constant_v0(const MinicC0Program *program,\n                                                          MinicExpressionId expression_id);\nbool minic_c0_assignment_compatible(const MinicC0Program *program,\n                                    MinicType target_type,\n                                    MinicExpressionId source_expression_id);",
)

old_helper = '''static bool expression_is_null_pointer_value(const MinicC0Program *program,
                                             MinicExpressionId expression_id) {
    size_t remaining;

    if (program == NULL) {
        return false;
    }
    remaining = program->expression_count + 1U;
    while (remaining > 0U) {
        const MinicExpression *expression;

        expression = minic_c0_program_expression(program, expression_id);
        if (expression == NULL) {
            return false;
        }
        if (expression->kind == MINIC_EXPRESSION_INTEGER) {
            return minic_type_is_integer(expression->type) && expression->value.integer_value == 0;
        }
        if ((expression->kind != MINIC_EXPRESSION_CAST &&
             expression->kind != MINIC_EXPRESSION_BITCAST) ||
            !minic_type_is_pointer(expression->type)) {
            return false;
        }
        expression_id = expression->value.unary.operand;
        remaining -= 1U;
    }
    return false;
}
'''
new_helper = '''bool minic_c0_expression_is_null_pointer_constant_v0(const MinicC0Program *program,
                                                          MinicExpressionId expression_id) {
    const MinicExpression *expression;
    const MinicExpression *operand;
    MinicType pointee;

    if (program == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_INTEGER) {
        return minic_type_is_integer(expression->type) && expression->value.integer_value == 0;
    }
    if ((expression->kind != MINIC_EXPRESSION_CAST &&
         expression->kind != MINIC_EXPRESSION_BITCAST) ||
        expression->type.pointer_depth != 1U ||
        !minic_type_pointee(expression->type, &pointee) || !minic_type_is_void(pointee)) {
        return false;
    }
    operand = minic_c0_program_expression(program, expression->value.unary.operand);
    return operand != NULL && operand->kind == MINIC_EXPRESSION_INTEGER &&
           minic_type_is_integer(operand->type) && operand->value.integer_value == 0;
}
'''
replace_once("src/frontend/ast.c", old_helper, new_helper)
replace_once(
    "src/frontend/ast.c",
    "           expression_is_null_pointer_value(program, source_expression_id);",
    "           minic_c0_expression_is_null_pointer_constant_v0(program, source_expression_id);",
)
replace_once(
    "src/frontend/ast.c",
    "            expression_is_null_pointer_value(program, right_expression_id)) ||\n           (expression_is_null_pointer_value(program, left_expression_id) &&",
    "            minic_c0_expression_is_null_pointer_constant_v0(program, right_expression_id)) ||\n           (minic_c0_expression_is_null_pointer_constant_v0(program, left_expression_id) &&",
)

# Conditional typing must consume expression-level NPC semantics before the
# type-only pointer composite rule. This preserves the non-null pointer arm type.
replace_once(
    "src/frontend/parser_expression.c",
    '''        if (!conditional_result_type(
                true_expression->type, false_expression->type, &conditional.type)) {
            minic_parser_error(parser, "conditional expression branches have incompatible types");
            return false;
        }
''',
    '''        if (minic_type_is_pointer(true_expression->type) &&
            minic_c0_expression_is_null_pointer_constant_v0(parser->program, when_false)) {
            conditional.type = true_expression->type;
        } else if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, when_true) &&
                   minic_type_is_pointer(false_expression->type)) {
            conditional.type = false_expression->type;
        } else if (!conditional_result_type(
                       true_expression->type, false_expression->type, &conditional.type)) {
            minic_parser_error(parser, "conditional expression branches have incompatible types");
            return false;
        }
''',
)

Path("tests/compiler/c0/conditional_null_pointer_constant.c").write_text(r'''struct node {
    const char *name;
    int value;
};

struct other_node {
    int value;
};

struct node *choose_integer_zero(int condition, struct node *node) {
    return condition ? node : 0;
}

struct node *choose_void_zero_right(int condition, struct node *node) {
    return condition ? node : (void *)0;
}

struct node *choose_void_zero_left(int condition, struct node *node) {
    return condition ? (void *)0 : node;
}

const struct node *choose_const_preserved(int condition, const struct node *node) {
    return condition ? node : (void *)0;
}

int member_after_conditional(int condition, struct node *node) {
    return (condition ? node : (void *)0)->value;
}

int linux_statement_expression_shape(int condition, struct node *node) {
    return ({
        struct node *saved = node;
        condition ? ({
            void *raw = (void *)saved;
            (struct node *)raw;
        }) : ((void *)0);
    })->value;
}
''')

Path("tests/compiler/c0/invalid_conditional_typed_null_pointer.c").write_text(r'''struct node { int value; };
struct other_node { int value; };
int bad(int condition, struct node *node) {
    return (condition ? node : (struct other_node *)0)->value;
}
''')

Path("tests/compiler/c0/invalid_conditional_nonnull_void_pointer.c").write_text(r'''struct node { int value; };
int bad(int condition, struct node *node) {
    return (condition ? node : (void *)1)->value;
}
''')

Path("tests/compiler/c0/run-conditional-null-pointer-constant.sh").write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-conditional-null-pointer

rm -rf "$work"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/conditional_null_pointer_constant.c" \
    -o "$work/conditional_null_pointer_constant.s"
test -s "$work/conditional_null_pointer_constant.s"
grep -F 'member_after_conditional:' "$work/conditional_null_pointer_constant.s" >/dev/null
grep -F 'linux_statement_expression_shape:' "$work/conditional_null_pointer_constant.s" >/dev/null

expect_failure() {
    source=$1
    message=$2
    if "$minic" -S "$root/tests/compiler/c0/$source.c" -o "$work/$source.s" \
        >"$work/$source.stdout" 2>"$work/$source.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$source: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$message" "$work/$source.stderr" >/dev/null || {
        cat "$work/$source.stderr" >&2
        exit 1
    }
}

expect_failure invalid_conditional_typed_null_pointer 'conditional expression branches have incompatible types'
expect_failure invalid_conditional_nonnull_void_pointer 'pointer member access requires a pointer to record'

printf '%s\n' 'PASS compiler/c0/conditional_null_pointer_constant integer-zero=pointer-type void-cast-zero=pointer-type qualifiers=preserved statement-expression=1 typed-pointer-zero=not-npc nonnull-void=not-npc'
''')

run_sh = Path("tests/compiler/c0/run.sh")
run_text = run_sh.read_text()
needle = 'MINIC="$minic" BUILD_DIR="$work/pragma-pack-record-layout" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-pragma-pack-record-layout.sh"\n'
if run_text.count(needle) != 1:
    raise SystemExit(f"run.sh pragma-pack anchor mismatch: {run_text.count(needle)}")
run_text = run_text.replace(
    needle,
    needle + '\nMINIC="$minic" BUILD_DIR="$work/conditional-null-pointer-constant" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-conditional-null-pointer-constant.sh"\n',
    1,
)
run_sh.write_text(run_text)
