#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one exact anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


p = Path("src/frontend/parser_expression.c")
text = p.read_text()
anchor = "bool minic_parser_apply_fixed_call_argument_conversion(MinicParser *parser,"
if text.count(anchor) != 1:
    raise SystemExit("fixed-call conversion anchor is not unique")
helper = r'''static bool gnu_function_pointer_bridge_call_conversion_compatible(
    const MinicC0Program *program, MinicType target, const MinicExpression *source) {
    const MinicExpression *bridge_operand;
    MinicType bridge_pointee;
    MinicType target_pointee;
    MinicType void_pointer;

    if (program == NULL || source == NULL || source->kind != MINIC_EXPRESSION_CAST ||
        !minic_type_pointer_to(minic_type_void(), &void_pointer) ||
        !minic_type_equal(source->type, void_pointer) ||
        !minic_type_pointee(target, &target_pointee) ||
        !minic_type_is_function(target_pointee)) {
        return false;
    }
    bridge_operand = minic_c0_program_expression(program, source->value.unary.operand);
    return bridge_operand != NULL &&
           minic_type_pointee(bridge_operand->type, &bridge_pointee) &&
           minic_type_is_function(bridge_pointee);
}

'''
p.write_text(text.replace(anchor, helper + anchor, 1))

replace_once(
    "src/frontend/parser_expression.c",
    '''        (minic_type_is_double(target_type) && minic_type_is_integer(source->type)) ||
        pointer_sign_call_conversion_compatible(target_type, source->type);
''',
    '''        (minic_type_is_double(target_type) && minic_type_is_integer(source->type)) ||
        pointer_sign_call_conversion_compatible(target_type, source->type) ||
        gnu_function_pointer_bridge_call_conversion_compatible(
            parser->program, target_type, source);
''',
    "fixed-call GNU function-pointer bridge conversion",
)

Path("tests/compiler/c0/gnu_function_pointer_bridge_call.c").write_text(r'''struct hlist_node {
    int value;
};

int __cpuhp_setup_state(int (*startup)(unsigned int cpu),
                        int (*teardown)(unsigned int cpu),
                        int multi_instance)
{
    (void)startup;
    (void)teardown;
    return multi_instance;
}

int cpuhp_setup_state_multi(int (*startup)(unsigned int cpu,
                                          struct hlist_node *node),
                            int (*teardown)(unsigned int cpu,
                                           struct hlist_node *node))
{
    return __cpuhp_setup_state((void *)startup, (void *)teardown, 1);
}

int startup_multi(unsigned int cpu, struct hlist_node *node)
{
    return (int)cpu + node->value;
}

int teardown_multi(unsigned int cpu, struct hlist_node *node)
{
    return (int)cpu - node->value;
}

int main(void)
{
    return cpuhp_setup_state_multi(startup_multi, teardown_multi) == 1 ? 0 : 1;
}
''')

Path("tests/compiler/c0/invalid_direct_incompatible_function_pointer_call.c").write_text(r'''struct hlist_node {
    int value;
};

int sink(int (*callback)(unsigned int cpu))
{
    (void)callback;
    return 0;
}

int multi(unsigned int cpu, struct hlist_node *node)
{
    return (int)cpu + node->value;
}

int main(void)
{
    return sink(multi);
}
''')

Path("tests/compiler/c0/invalid_void_pointer_variable_function_call.c").write_text(r'''int sink(int (*callback)(unsigned int cpu))
{
    (void)callback;
    return 0;
}

int bridge(void *opaque)
{
    return sink(opaque);
}

int main(void)
{
    return 0;
}
''')

Path("tests/compiler/c0/run-gnu-function-pointer-bridge-call.sh").write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-function-pointer-bridge-call

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_function_pointer_bridge_call.c" \
    -o "$work/bridge.i"
"$minic" -S "$work/bridge.i" -o "$work/bridge.s"
grep -F '  call __cpuhp_setup_state' "$work/bridge.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_function_pointer_bridge_call explicit-void-bridge=2 target-bitcast=1 direct-incompatible=still-strict'

for name in invalid_direct_incompatible_function_pointer_call \
            invalid_void_pointer_variable_function_call; do
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F 'call argument type does not match declaration' "$work/$name.stderr" >/dev/null
    printf '%s\n' "PASS compiler/c0/$name"
done
''')

run = Path("tests/compiler/c0/run.sh")
text = run.read_text()
marker = '''MINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-function-pointer-qualifiers.sh"\n'''
insert = marker + '''\nMINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-gnu-function-pointer-bridge-call.sh"\n'''
if text.count(marker) != 1:
    raise SystemExit("C0 function-pointer gate insertion anchor is not unique")
run.write_text(text.replace(marker, insert, 1))
