#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def read(p): return (ROOT / p).read_text()
def write(p, s): (ROOT / p).write_text(s)
def one(s, old, new, label):
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"{label}: expected 1 match, found {n}")
    return s.replace(old, new, 1)

p = "src/frontend/parser_function.c"
s = read(p)
s = one(s,
'''static bool parse_function_signature_type_name(MinicParser *parser, MinicType *type) {
    MinicType base_type;

    if (parser == NULL || type == NULL || !minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_pointer_declarator(parser, base_type, type)) {
        return false;
    }
    if (minic_type_is_record(*type)) {
        return minic_parser_require_complete_object_type(
            parser, *type, "incomplete record type requires pointer declarator");
    }
    return true;
}
''',
'''static bool parse_function_signature_type_name(MinicParser *parser, MinicType *type) {
    MinicType base_type;

    /* A function declaration may preserve an incomplete record/enum by value in
     * its signature. Completeness becomes mandatory only when a definition
     * materializes the return/parameter ABI or when ordinary object storage is
     * created. Keep this parser about signature identity, not storage. */
    return parser != NULL && type != NULL &&
           minic_parser_parse_type_specifiers(parser, &base_type) &&
           minic_parser_parse_pointer_declarator(parser, base_type, type);
}
''', "signature type parser")
s = one(s,
'''    if (!apply_function_attribute_list(
            parser,
            &deferred_attributes,
            true,
            is_internal,
            is_inline,
            "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must "
            "be implemented explicitly") ||
        (minic_type_is_record(return_type) &&
         !minic_parser_require_complete_object_type(
             parser, return_type, "incomplete record type requires pointer declarator"))) {
        return false;
    }
''',
'''    if (!apply_function_attribute_list(
            parser,
            &deferred_attributes,
            true,
            is_internal,
            is_inline,
            "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must "
            "be implemented explicitly")) {
        return false;
    }
''', "declaration return completion")
s = one(s,
'''        for (parameter_index = 0U; parameter_index < parameter_count; ++parameter_index) {
            if (minic_type_is_enum(parameter_types[parameter_index]) &&
                !minic_parser_require_complete_object_type(
                    parser,
                    parameter_types[parameter_index],
                    "function definition requires complete enum parameter types")) {
                return false;
            }
        }
''',
'''        for (parameter_index = 0U; parameter_index < parameter_count; ++parameter_index) {
            if ((minic_type_is_record(parameter_types[parameter_index]) ||
                 minic_type_is_enum(parameter_types[parameter_index])) &&
                !minic_parser_require_complete_object_type(
                    parser,
                    parameter_types[parameter_index],
                    "function definition requires complete object parameter types")) {
                return false;
            }
        }
''', "definition parameter completion")
write(p, s)

write("tests/compiler/c0/gnu_incomplete_record_function_signature.c", r'''struct Range;

/* Linux memory_hotplug.h shape: declaration may return an incomplete record. */
struct Range arch_get_mappable_range(void);
void consume_range(struct Range);
typedef struct Range (*range_transform_t)(struct Range);

struct SignatureHolder {
    range_transform_t transform;
};

struct Range {
    unsigned long start;
    unsigned long end;
};

/* Completion must preserve the same record identity in all earlier signatures. */
struct Range arch_get_mappable_range(void);
void consume_range(struct Range);

typedef struct Range (*range_transform_after_t)(struct Range);
_Static_assert(__builtin_types_compatible_p(range_transform_t, range_transform_after_t),
               "forward and completed record signatures must retain identity");
''')

write("tests/compiler/c0/run-gnu-incomplete-record-function-signature.sh", r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-incomplete-record-function-signature

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/gnu_incomplete_record_function_signature.c" \
  -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"

cat >"$work/incomplete-return-definition.c" <<'EOF'
struct Pending;
struct Pending make_pending(void) { }
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/incomplete-return-definition.c" \
  -o "$work/incomplete-return-definition.i"
if "$minic" -S "$work/incomplete-return-definition.i" \
    -o "$work/incomplete-return-definition.s" \
    2>"$work/incomplete-return-definition.stderr"; then
  printf '%s\n' 'FAIL compiler/c0/gnu_incomplete_record_function_signature: incomplete return definition accepted' >&2
  exit 1
fi
grep -F 'function definition requires a complete return type' \
  "$work/incomplete-return-definition.stderr" >/dev/null

cat >"$work/incomplete-parameter-definition.c" <<'EOF'
struct Pending;
void consume_pending(struct Pending value) { }
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/incomplete-parameter-definition.c" \
  -o "$work/incomplete-parameter-definition.i"
if "$minic" -S "$work/incomplete-parameter-definition.i" \
    -o "$work/incomplete-parameter-definition.s" \
    2>"$work/incomplete-parameter-definition.stderr"; then
  printf '%s\n' 'FAIL compiler/c0/gnu_incomplete_record_function_signature: incomplete parameter definition accepted' >&2
  exit 1
fi
grep -F 'function definition requires complete object parameter types' \
  "$work/incomplete-parameter-definition.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_incomplete_record_function_signature declaration-return=1 declaration-parameter=1 function-pointer=1 completion=same-record-id definition-return=complete-required definition-parameter=complete-required'
''')

p = "tools/dev/pr76-focused.sh"
s = read(p)
s = one(s,
'''sh tests/compiler/c0/run-record-forward-declarations.sh
''',
'''sh tests/compiler/c0/run-record-forward-declarations.sh
sh tests/compiler/c0/run-gnu-incomplete-record-function-signature.sh
''', "focused registration")
write(p, s)
