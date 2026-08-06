#!/usr/bin/env bash
set -Eeuo pipefail

cat >src/frontend/parser_record.c <<'EOF'
#include "frontend/parser_internal.h"

#include <string.h>

static bool
record_has_field(const MinicParser *parser, const MinicRecord *record, MinicSourceSpan name_span) {
    size_t name_length;
    size_t index;

    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < record->field_count; ++index) {
        const MinicRecordField *field;

        field = minic_c0_record_field(record, index);
        if (field != NULL && field->name_length == name_length &&
            memcmp(field->name, parser->source + name_span.begin.offset, name_length) == 0) {
            return true;
        }
    }
    return false;
}

static bool parse_record_field(MinicParser *parser, MinicRecordId record_id) {
    MinicSourceSpan name_span;
    MinicType base_type;
    MinicType field_type;
    size_t element_count;
    const MinicRecord *record;

    if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_pointer_declarator(parser, base_type, &field_type)) {
        return false;
    }
    if (minic_type_is_void(field_type)) {
        minic_parser_error(parser, "record field cannot have void type");
        return false;
    }
    if (minic_type_is_array(field_type)) {
        minic_parser_error(parser, "record field typedef array is unsupported");
        return false;
    }
    if (!minic_parser_require_complete_object_type(
            parser, field_type, "record field cannot use incomplete type by value")) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected record field name");
        return false;
    }

    name_span = parser->current.span;
    record = minic_c0_program_record(parser->program, record_id);
    if (record == NULL) {
        minic_parser_error(parser, "invalid record while adding field");
        return false;
    }
    if (record_has_field(parser, record, name_span)) {
        minic_parser_error(parser, "duplicate record field");
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }

    element_count = 1U;
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_fixed_array_bound(parser, &element_count)) {
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record field")) {
        return false;
    }
    if (!minic_c0_record_add_field(parser->program,
                                   record_id,
                                   parser->source + name_span.begin.offset,
                                   minic_parser_span_length(name_span),
                                   field_type,
                                   element_count)) {
        minic_parser_error(parser, "out of memory while adding record field");
        return false;
    }
    return true;
}

bool minic_parser_parse_record_definition_specifier(MinicParser *parser, MinicType *record_type) {
    MinicSourceSpan name_span;
    MinicRecordId record_id;

    if (record_type == NULL) {
        minic_parser_error(parser, "internal error: missing record type output");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_STRUCT, "expected keyword 'struct'")) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected record tag after 'struct'");
        return false;
    }

    name_span = parser->current.span;
    if (minic_parser_find_record(parser, name_span) != MINIC_RECORD_INVALID) {
        minic_parser_error(parser, "duplicate record definition");
        return false;
    }
    if (!minic_c0_program_add_record(parser->program,
                                     parser->source + name_span.begin.offset,
                                     minic_parser_span_length(name_span),
                                     &record_id)) {
        minic_parser_error(parser, "out of memory while adding record");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' after record tag")) {
        return false;
    }

    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (parser->current.kind == MINIC_TOKEN_EOF) {
            minic_parser_error(parser, "expected '}' before end of record");
            return false;
        }
        if (!parse_record_field(parser, record_id)) {
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after record fields")) {
        return false;
    }
    if (!minic_c0_program_finish_record(parser->program, record_id)) {
        minic_parser_error(parser, "record definition requires at least one field");
        return false;
    }
    *record_type = minic_type_record(record_id);
    return true;
}

bool minic_parser_parse_record_definition(MinicParser *parser) {
    MinicType record_type;

    return minic_parser_parse_record_definition_specifier(parser, &record_type) &&
           minic_parser_expect(
               parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record definition");
}
EOF

cat >src/frontend/parser_typedef.c <<'EOF'
#include "frontend/parser_internal.h"

#include <string.h>

MinicTypeAliasId minic_parser_find_type_alias(const MinicParser *parser,
                                              MinicSourceSpan name_span) {
    size_t name_length;
    size_t index;

    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->type_alias_count; ++index) {
        const MinicTypeAlias *alias;

        alias = minic_c0_program_type_alias(parser->program, index);
        if (alias != NULL && alias->name_length == name_length &&
            memcmp(alias->name, parser->source + name_span.begin.offset, name_length) == 0) {
            return index;
        }
    }
    return MINIC_TYPE_ALIAS_INVALID;
}

static bool typedef_starts_record_definition(MinicParser *parser, bool *is_definition) {
    MinicParser probe;

    if (parser == NULL || is_definition == NULL) {
        return false;
    }
    *is_definition = false;
    if (parser->current.kind != MINIC_TOKEN_KW_STRUCT) {
        return true;
    }

    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind != MINIC_TOKEN_IDENTIFIER) {
        return true;
    }
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    *is_definition = probe.current.kind == MINIC_TOKEN_LBRACE;
    return true;
}

bool minic_parser_parse_typedef(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType aliased_type;
    MinicTypeAliasId alias_id;
    size_t bounds[8];
    size_t bound_count;
    bool is_record_definition;

    bound_count = 0U;
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_TYPEDEF, "expected keyword 'typedef'") ||
        !typedef_starts_record_definition(parser, &is_record_definition)) {
        return false;
    }
    if (is_record_definition) {
        if (!minic_parser_parse_record_definition_specifier(parser, &aliased_type)) {
            return false;
        }
    } else if (!minic_parser_parse_type_name(parser, &aliased_type)) {
        return false;
    }
    if (minic_type_is_void(aliased_type)) {
        minic_parser_error(parser, "typedef cannot name bare void");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected typedef name");
        return false;
    }

    name_span = parser->current.span;
    if (minic_parser_find_type_alias(parser, name_span) != MINIC_TYPE_ALIAS_INVALID) {
        minic_parser_error(parser, "duplicate typedef name");
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }

    while (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (bound_count >= sizeof(bounds) / sizeof(bounds[0])) {
            minic_parser_error(parser, "at most eight array dimensions are supported");
            return false;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_fixed_array_bound(parser, &bounds[bound_count])) {
            return false;
        }
        bound_count += 1U;
    }

    while (bound_count > 0U) {
        bound_count -= 1U;
        if (!minic_c0_program_add_array_type(
                parser->program, aliased_type, bounds[bound_count], &aliased_type)) {
            minic_parser_error(parser, "out of memory while building typedef array type");
            return false;
        }
    }
    if (parser->current.kind != MINIC_TOKEN_SEMICOLON) {
        minic_parser_error(parser, "expected ';' after typedef");
        return false;
    }
    if (!minic_c0_program_add_type_alias(parser->program,
                                         parser->source + name_span.begin.offset,
                                         minic_parser_span_length(name_span),
                                         aliased_type,
                                         &alias_id)) {
        minic_parser_error(parser, "out of memory while adding typedef");
        return false;
    }
    return minic_parser_advance(parser);
}
EOF

python3 - <<'PY'
from pathlib import Path


def replace_once(path, old, new):
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one marker, found {count}")
    file.write_text(text.replace(old, new, 1))

replace_once(
    "src/frontend/parser_internal.h",
    '''bool minic_parser_parse_type_name(MinicParser *parser, MinicType *type);
bool minic_parser_parse_fixed_array_bound''',
    '''bool minic_parser_parse_type_name(MinicParser *parser, MinicType *type);
bool minic_parser_require_complete_object_type(MinicParser *parser,
                                               MinicType type,
                                               const char *message);
bool minic_parser_parse_fixed_array_bound''',
)
replace_once(
    "src/frontend/parser_internal.h",
    '''bool minic_parser_parse_record_definition(MinicParser *parser);
bool minic_parser_parse_typedef''',
    '''bool minic_parser_parse_record_definition_specifier(MinicParser *parser,
                                                          MinicType *record_type);
bool minic_parser_parse_record_definition(MinicParser *parser);
bool minic_parser_parse_typedef''',
)

parser_type = Path("src/frontend/parser_type.c")
text = parser_type.read_text()
insert = '''bool minic_parser_require_complete_object_type(MinicParser *parser,
                                               MinicType type,
                                               const char *message) {
    const MinicRecord *record;

    if (!minic_type_is_record(type)) {
        return true;
    }
    record = minic_c0_program_record(parser->program, type.record_id);
    if (record != NULL && record->is_complete) {
        return true;
    }
    minic_parser_error(parser, "%s", message);
    return false;
}

'''
marker = "bool minic_parser_parse_type_specifiers(MinicParser *parser, MinicType *type) {\n"
if text.count(marker) != 1:
    raise SystemExit("parser_type function marker mismatch")
text = text.replace(marker, insert + marker, 1)
old = '''        record_id = minic_parser_find_record(parser, parser->current.span);
        record = minic_c0_program_record(parser->program, record_id);
        if (record == NULL || !record->is_complete) {
            minic_parser_error(parser, "use of undeclared record tag");
            return false;
        }
'''
new = '''        record_id = minic_parser_find_record(parser, parser->current.span);
        record = minic_c0_program_record(parser->program, record_id);
        if (record == NULL) {
            minic_parser_error(parser, "use of undeclared record tag");
            return false;
        }
'''
if text.count(old) != 1:
    raise SystemExit("parser_type record lookup marker mismatch")
text = text.replace(old, new, 1)
old = '''bool minic_parser_parse_type_name(MinicParser *parser, MinicType *type) {
    MinicType base_type;

    return minic_parser_parse_type_specifiers(parser, &base_type) &&
           minic_parser_parse_pointer_declarator(parser, base_type, type);
}
'''
new = '''bool minic_parser_parse_type_name(MinicParser *parser, MinicType *type) {
    MinicType base_type;

    if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_pointer_declarator(parser, base_type, type)) {
        return false;
    }
    return minic_parser_require_complete_object_type(
        parser, *type, "incomplete record type requires pointer declarator");
}
'''
if text.count(old) != 1:
    raise SystemExit("parser_type type-name marker mismatch")
parser_type.write_text(text.replace(old, new, 1))

replace_once(
    "Makefile",
    '''\tcheck-unsigned-declarations check-long-types check-for-loops check-unbounded-for-break \\
''',
    '''\tcheck-unsigned-declarations check-long-types check-self-referential-records \\
\tcheck-for-loops check-unbounded-for-break \\
''',
)
replace_once(
    "Makefile",
    '''\t\t"  make check-long-types Run signed/unsigned long declaration gates" \\
''',
    '''\t\t"  make check-long-types Run signed/unsigned long declaration gates" \\
\t\t"  make check-self-referential-records Run incomplete-tag and self-reference gates" \\
''',
)
replace_once(
    "Makefile",
    '''check-for-loops: $(MINIC_BINARY)
''',
    '''check-self-referential-records: $(MINIC_BINARY)
\tMINIC="$(abspath $(MINIC_BINARY))" \\
\tHOST_CC="$(CC)" \\
\tBUILD_DIR="$(abspath $(BUILD_DIR))" \\
\tsh tests/compiler/c0/run-self-referential-records.sh

check-for-loops: $(MINIC_BINARY)
''',
)
replace_once(
    "Makefile",
    '''check-static-functions check-unsigned-declarations check-long-types check-for-loops''',
    '''check-static-functions check-unsigned-declarations check-long-types check-self-referential-records check-for-loops''',
)

manifest = Path("tests/programs/c0/manifest.txt")
text = manifest.read_text()
if "self_referential_record\n" in text:
    raise SystemExit("self-referential program already registered")
manifest.write_text(text + "self_referential_record\n")
PY

cat >tests/programs/c0/self_referential_record.c <<'EOF'
typedef struct Node {
    struct Node *next;
    int value;
} Node;

int main(void)
{
    Node first;
    Node second;

    first.next = &second;
    first.next->value = 41;
    return first.next->value;
}
EOF

cat >tests/compiler/c0/invalid_self_record_by_value.c <<'EOF'
typedef struct Node {
    struct Node next;
    int value;
} Node;

int main(void)
{
    return 0;
}
EOF

cat >tests/compiler/c0/invalid_unknown_record_pointer.c <<'EOF'
int main(void)
{
    struct Missing *value;
    return value == value;
}
EOF

cat >tests/compiler/c0/run-self-referential-records.sh <<'EOF'
#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-self-records

mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/programs/c0/self_referential_record.c" \
    -o "$work/self_referential_record.i"
"$minic" -S "$work/self_referential_record.i" -o "$work/self_referential_record.s"
grep -F ".globl main" "$work/self_referential_record.s" >/dev/null
printf '%s\n' "PASS compiler/c0/self_referential_record"

check_invalid() {
    name=$1
    expected=$2

    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$expected" "$work/$name.stderr" >/dev/null
    printf '%s\n' "PASS compiler/c0/$name"
}

check_invalid invalid_self_record_by_value \
    "record field cannot use incomplete type by value"
check_invalid invalid_unknown_record_pointer \
    "use of undeclared record tag"
EOF

chmod +x tests/compiler/c0/run-self-referential-records.sh
clang-format-18 -i \
  src/frontend/parser_internal.h \
  src/frontend/parser_record.c \
  src/frontend/parser_type.c \
  src/frontend/parser_typedef.c

make -j2 check-fast

sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  gcc-riscv64-linux-gnu libc6-dev-riscv64-cross qemu-user
make -j2 MODE=release BUILD_DIR=build/self-records
MINIC="$PWD/build/self-records/bin/minic" \
BUILD_DIR="$PWD/build/self-records" \
RISCV_CC=riscv64-linux-gnu-gcc \
RISCV_OBJDUMP=riscv64-linux-gnu-objdump \
QEMU_RISCV64=qemu-riscv64 \
REQUIRE_RISCV_RUNTIME=1 \
  sh tests/programs/c0/run.sh

git config user.name "github-actions[bot]"
git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
git rm \
  .github/scripts/apply-self-referential-records.sh \
  .github/workflows/apply-self-referential-records.yml \
  .github/workflows/discover-incomplete-record-context.yml
git add Makefile src tests
git commit -m "frontend: introduce self-referential record tags" -m "Introduce tagged records before parsing their fields, reuse the same record identity through completion, support inline typedef record definitions, and permit incomplete records only through pointers. Add focused negative gates and the forty-first GCC/MiniC differential program for a linked self-referential record.\n\n中文说明：在解析字段前引入结构体标签，完成定义期间复用同一记录身份，支持 typedef 内联带标签结构体定义，并仅允许通过指针使用不完整记录；增加聚焦负例和第 41 个 GCC/MiniC 自引用链式记录差分程序。\n\nValidation / 验证： complete host fast gate and 41 RV64/QEMU differential programs PASS."
git push origin HEAD:frontend/incomplete-record-tags
