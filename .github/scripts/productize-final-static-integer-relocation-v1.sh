#!/usr/bin/env bash
set -Eeuo pipefail

git fetch origin diagnostic/effective-convergence-snapshot-v1
git show origin/diagnostic/effective-convergence-snapshot-v1:tools/dev/materialize-static-integer-address-relocation-v1.py > tools/dev/materialize-static-integer-address-relocation-v1.py
python3 tools/dev/materialize-static-integer-address-relocation-v1.py
rm tools/dev/materialize-static-integer-address-relocation-v1.py

# The convergence materializer proved the relocation semantics, but its
# DataLayout traversal called a GlobalObject-owned slot-count wrapper. Promote
# that purely structural query to AST core so target layout remains linkable
# with ast.o alone.
python3 - <<'PY'
from pathlib import Path

h = Path('src/frontend/ast.h')
text = h.read_text()
wrapper_decl = '''bool minic_c0_global_initializer_slot_count(const MinicC0Program *program,
                                            MinicType type,
                                            size_t *slot_count);
'''
core_decl = '''bool minic_c0_type_initializer_slot_count(const MinicC0Program *program,
                                          MinicType type,
                                          size_t *slot_count);
'''
if text.count(wrapper_decl) != 1:
    raise SystemExit(f'ast.h slot-count wrapper declaration count={text.count(wrapper_decl)}')
if core_decl not in text:
    text = text.replace(wrapper_decl, core_decl + wrapper_decl, 1)
h.write_text(text)

a = Path('src/frontend/ast.c')
text = a.read_text()
if 'bool minic_c0_type_initializer_slot_count(' not in text:
    anchor = 'bool minic_c0_program_add_expression(MinicC0Program *program,\n'
    if text.count(anchor) != 1:
        raise SystemExit(f'ast.c insertion anchor count={text.count(anchor)}')
    impl = r'''static bool minic_c0_type_initializer_slot_count_impl(const MinicC0Program *program,
                                                       MinicType type,
                                                       size_t *slot_count) {
    if (program == NULL || slot_count == NULL) {
        return false;
    }
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        *slot_count = 1U;
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        size_t element_slots;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U ||
            !minic_c0_type_initializer_slot_count_impl(
                program, array_type->element_type, &element_slots) ||
            (element_slots != 0U && array_type->element_count > SIZE_MAX / element_slots)) {
            return false;
        }
        *slot_count = array_type->element_count * element_slots;
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t field_index;
        size_t field_limit;
        size_t total;

        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete) {
            return false;
        }
        field_limit = record->is_union ? 1U : record->field_count;
        total = 0U;
        for (field_index = 0U; field_index < field_limit; ++field_index) {
            const MinicRecordField *field;
            size_t element_slots;
            size_t field_slots;

            field = &record->fields[field_index];
            if (field->element_count == 0U || field->is_flexible_array ||
                !minic_c0_type_initializer_slot_count_impl(
                    program, field->type, &element_slots) ||
                (element_slots != 0U && field->element_count > SIZE_MAX / element_slots)) {
                return false;
            }
            field_slots = field->element_count * element_slots;
            if (total > SIZE_MAX - field_slots) {
                return false;
            }
            total += field_slots;
        }
        *slot_count = total;
        return true;
    }
    return false;
}

bool minic_c0_type_initializer_slot_count(const MinicC0Program *program,
                                          MinicType type,
                                          size_t *slot_count) {
    return minic_c0_type_initializer_slot_count_impl(program, type, slot_count);
}

'''
    text = text.replace(anchor, impl + anchor, 1)
a.write_text(text)

g = Path('src/frontend/ast_global.c')
text = g.read_text()
helper_start = 'static bool\naggregate_scalar_slot_count('
wrapper_start = '\nbool minic_c0_global_initializer_slot_count('
if helper_start not in text or wrapper_start not in text:
    raise SystemExit('ast_global slot-count ownership anchors missing')
start = text.index(helper_start)
end = text.index(wrapper_start, start)
text = text[:start] + text[end + 1:]
old_wrapper = '''bool minic_c0_global_initializer_slot_count(const MinicC0Program *program,
                                            MinicType type,
                                            size_t *slot_count) {
    return aggregate_scalar_slot_count(program, type, slot_count);
}
'''
new_wrapper = '''bool minic_c0_global_initializer_slot_count(const MinicC0Program *program,
                                            MinicType type,
                                            size_t *slot_count) {
    return minic_c0_type_initializer_slot_count(program, type, slot_count);
}
'''
if text.count(old_wrapper) != 1:
    raise SystemExit(f'ast_global wrapper body count={text.count(old_wrapper)}')
text = text.replace(old_wrapper, new_wrapper, 1)
text = text.replace('aggregate_scalar_slot_count(', 'minic_c0_type_initializer_slot_count(')
g.write_text(text)

d = Path('src/target/data_layout.c')
text = d.read_text()
dependency = 'minic_c0_global_initializer_slot_count('
if text.count(dependency) != 2:
    raise SystemExit(f'data-layout global slot-count dependency count={text.count(dependency)}')
text = text.replace(dependency, 'minic_c0_type_initializer_slot_count(')
d.write_text(text)
PY

cat > tests/compiler/c0/static_integer_address_relocation.c <<'EOF'
static unsigned char init_stack[128];

struct thread_state {
    unsigned long sp;
};

struct task_state {
    struct thread_state thread;
};

static struct task_state init_task = {
    .thread = {
        .sp = sizeof(init_stack) + (unsigned long)&init_stack,
    },
};

int main(void) {
    return init_task.thread.sp == 0UL;
}
EOF

cat > tests/compiler/c0/run-static-integer-address-relocation.sh <<'EOF'
#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/static-integer-address-relocation
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/static_integer_address_relocation.c" \
  -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -q 'init_stack' "$work/output.s"
EOF
chmod +x tests/compiler/c0/run-static-integer-address-relocation.sh

python3 - <<'PY'
from pathlib import Path
path = Path('tests/compiler/c0/run.sh')
source = path.read_text()
invocation = '''\nMINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
  sh "$root/tests/compiler/c0/run-static-integer-address-relocation.sh"\n'''
if 'run-static-integer-address-relocation.sh' not in source:
    path.write_text(source + invocation)
PY

mapfile -t changed_c < <(git diff --name-only -- '*.c' '*.h')
if [ "${#changed_c[@]}" -gt 0 ]; then
  clang-format-18 -i "${changed_c[@]}"
fi
CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh check
git diff --check
make -j4 MODE=release CFLAGS=-Werror BUILD_DIR=build/product-static-integer-reloc
MINIC="$GITHUB_WORKSPACE/build/product-static-integer-reloc/bin/minic" \
BUILD_DIR="$GITHUB_WORKSPACE/build/product-static-integer-reloc" \
  sh tests/compiler/c0/run-static-integer-address-relocation.sh
make -j4 check-fast MODE=release BUILD_DIR=build/product-static-integer-reloc-fast

rm -f diagnostics/final-static-integer-relocation-trigger.txt
git config user.name github-actions[bot]
git config user.email 41898282+github-actions[bot]@users.noreply.github.com
git add -A
git commit -m 'frontend: support symbolic integer static relocations'
git push origin HEAD:product/final-static-integer-relocation-v1
