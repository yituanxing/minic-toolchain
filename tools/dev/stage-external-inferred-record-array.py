#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:140]!r}")
    target.write_text(text.replace(old, new, 1))


path = Path("src/frontend/parser_function.c")
text = path.read_text()
marker = """static bool parse_visible_external_array(MinicParser *parser,
                                         MinicType element_type,
"""
helper = r'''static bool probe_braced_external_record_array_element_count(MinicParser *parser,
                                                              size_t *element_count) {
    MinicParser probe;
    size_t count;

    if (parser == NULL || element_count == NULL || parser->current.kind != MINIC_TOKEN_EQUAL) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_LBRACE ||
        !minic_parser_advance(&probe)) {
        minic_parser_error(parser, "inferred external record array requires a braced initializer");
        return false;
    }

    count = 0U;
    while (probe.current.kind != MINIC_TOKEN_RBRACE) {
        size_t brace_depth;

        if (probe.current.kind != MINIC_TOKEN_LBRACE) {
            minic_parser_error(
                parser,
                "inferred external record array requires braced record elements");
            return false;
        }
        brace_depth = 0U;
        for (;;) {
            if (probe.current.kind == MINIC_TOKEN_EOF) {
                minic_parser_error(parser, "unterminated inferred external record array initializer");
                return false;
            }
            if (probe.current.kind == MINIC_TOKEN_LBRACE) {
                brace_depth += 1U;
            } else if (probe.current.kind == MINIC_TOKEN_RBRACE) {
                if (brace_depth == 0U) {
                    minic_parser_error(parser, "invalid inferred external record array shape");
                    return false;
                }
                brace_depth -= 1U;
                if (brace_depth == 0U) {
                    if (!minic_parser_advance(&probe)) {
                        return false;
                    }
                    break;
                }
            }
            if (!minic_parser_advance(&probe)) {
                return false;
            }
        }
        if (count == SIZE_MAX) {
            minic_parser_error(parser, "inferred external record array element count overflows");
            return false;
        }
        count += 1U;
        if (probe.current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(&probe)) {
                return false;
            }
            if (probe.current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (probe.current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(
                parser,
                "expected ',' or '}' after inferred external record array element");
            return false;
        }
    }
    if (count == 0U) {
        minic_parser_error(parser, "inferred external record array requires at least one element");
        return false;
    }
    *element_count = count;
    return true;
}

static bool parse_inferred_external_record_array_definition(
    MinicParser *parser,
    MinicType element_type,
    MinicSourceSpan name_span,
    char *section_name,
    size_t section_name_capacity,
    size_t *section_name_length,
    bool *has_section,
    size_t *explicit_alignment,
    MinicSymbolVisibility visibility,
    bool has_visibility) {
    const MinicRecord *record;
    MinicType array_type;
    size_t element_count;

    if (parser == NULL || section_name == NULL || section_name_length == NULL ||
        has_section == NULL || explicit_alignment == NULL || !minic_type_is_record(element_type) ||
        parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    record = minic_c0_program_record(parser->program, element_type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser,
                           "inferred external record array requires a complete record element type");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser,
                             MINIC_TOKEN_RBRACKET,
                             "expected ']' in inferred external record array") ||
        !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_name_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        minic_parser_error(parser, "incomplete external tentative array is not implemented yet");
        return false;
    }
    if (!probe_braced_external_record_array_element_count(parser, &element_count) ||
        !minic_c0_program_add_array_type(
            parser->program, element_type, element_count, &array_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot complete inferred external record array type");
        }
        return false;
    }
    return parse_external_object_definition(parser,
                                            array_type,
                                            name_span,
                                            section_name,
                                            *section_name_length,
                                            *has_section,
                                            *explicit_alignment,
                                            visibility,
                                            has_visibility);
}

'''
if text.count(marker) != 1:
    raise SystemExit("parser_function.c: visible external array marker not unique")
path.write_text(text.replace(marker, helper + marker, 1))

old = """        if (probe.current.kind == MINIC_TOKEN_SEMICOLON) {
            minic_parser_error(parser,
                               "incomplete external tentative array is not implemented yet");
            return false;
        }
        return parse_external_integer_array_definition(parser, element_type, name_span);
"""
new = """        if (probe.current.kind == MINIC_TOKEN_SEMICOLON) {
            minic_parser_error(parser,
                               "incomplete external tentative array is not implemented yet");
            return false;
        }
        if (minic_type_is_record(element_type)) {
            return parse_inferred_external_record_array_definition(parser,
                                                                   element_type,
                                                                   name_span,
                                                                   section_name,
                                                                   section_name_capacity,
                                                                   section_name_length,
                                                                   has_section,
                                                                   explicit_alignment,
                                                                   visibility,
                                                                   has_visibility);
        }
        return parse_external_integer_array_definition(parser, element_type, name_span);
"""
replace_once("src/frontend/parser_function.c", old, new)

Path("tests/compiler/c0/external_inferred_record_array.c").write_text(r'''struct riscv_isa_ext_data {
    const char *name;
    const char *property;
    unsigned int id;
};

extern const struct riscv_isa_ext_data riscv_isa_ext[];

const struct riscv_isa_ext_data riscv_isa_ext[] = {
    {
        .name = "zicntr",
        .property = "riscv,isa-base",
        .id = 1,
    },
    {
        .name = "zifencei",
        .property = 0,
        .id = 2,
    },
};

struct stats_desc {
    unsigned int flags;
    int exponent;
    unsigned int size;
    unsigned int bucket_size;
    unsigned int offset;
};

struct kvm_stats_desc {
    struct stats_desc desc;
    char name[16];
};

const struct kvm_stats_desc kvm_stats[] = {
    {
        {
            .flags = 1,
            .exponent = -9,
            .size = 1,
            .bucket_size = 0,
            .offset = 4,
        },
        .name = "halt_wait",
    },
    {
        {
            .flags = 2,
            .exponent = 0,
            .size = 8,
            .bucket_size = 1,
            .offset = 12,
        },
        .name = "signal",
    },
};

const struct riscv_isa_ext_data fixed_record_array[1] = {
    {
        .name = "fixed",
        .property = 0,
        .id = 3,
    },
};
''')

Path("tests/compiler/c0/invalid_external_inferred_record_array_brace_elision.c").write_text(
    "struct pair { int left; int right; };\n"
    "const struct pair pairs[] = { 1, 2 };\n"
)

Path("tests/compiler/c0/run-external-inferred-record-array.sh").write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-external-inferred-record-array
asm="$work/external_inferred_record_array.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/external_inferred_record_array.c" \
    -o "$work/external_inferred_record_array.i"
"$minic" -S "$work/external_inferred_record_array.i" -o "$asm"

grep -F 'riscv_isa_ext:' "$asm" >/dev/null
grep -F '.size riscv_isa_ext, 48' "$asm" >/dev/null
grep -F 'kvm_stats:' "$asm" >/dev/null
grep -F '.size kvm_stats, 72' "$asm" >/dev/null
grep -F 'fixed_record_array:' "$asm" >/dev/null
grep -F '.size fixed_record_array, 24' "$asm" >/dev/null

test "$(grep -c '^  \.dword \.Lminic_string_' "$asm")" -ge 4

after_label=$(sed -n '/^kvm_stats:/,/^.size kvm_stats, 72/p' "$asm")
printf '%s\n' "$after_label" | grep -F '104' >/dev/null
printf '%s\n' "$after_label" | grep -F '97' >/dev/null

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/invalid_external_inferred_record_array_brace_elision.c" \
    -o "$work/invalid.i"
if "$minic" -S "$work/invalid.i" -o "$work/invalid.s" \
    >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/invalid_external_inferred_record_array_brace_elision: unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'inferred external record array requires braced record elements' \
    "$work/invalid.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/external_inferred_record_array bound=shape-prepass record=nested+designated pointer-reloc=1 char-array-string=1 fixed-path=unchanged brace-elision=fail-closed'
''')

run_path = Path("tests/compiler/c0/run.sh")
run_text = run_path.read_text()
needle = 'run-external-inferred-record-array.sh'
if needle in run_text:
    raise SystemExit("run.sh: inferred record-array runner already wired")
run_path.write_text(
    run_text.rstrip()
    + '\n\nMINIC="$minic" \\\nHOST_CC="$host_cc" \\\nBUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\nsh "$root/tests/compiler/c0/run-external-inferred-record-array.sh"\n'
)

print("staged inferred external record-array composition")
