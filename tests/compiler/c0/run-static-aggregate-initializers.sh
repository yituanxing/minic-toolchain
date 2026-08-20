#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
build_dir=${BUILD_DIR:-"$root/build/static-aggregate-initializers"}
mkdir -p "$build_dir"

"$minic" -S "$root/tests/programs/c0/static_record_compound_literal.c" \
    -o "$build_dir/static_record_compound_literal.s"
grep -F '.word 3735899821' "$build_dir/static_record_compound_literal.s" >/dev/null
grep -F '.dword -1' "$build_dir/static_record_compound_literal.s" >/dev/null
test "$(grep -c '  .dword value+' "$build_dir/static_record_compound_literal.s")" -eq 2

"$minic" -S "$root/tests/programs/c0/static_nested_record_nonzero.c" \
    -o "$build_dir/static_nested_record_nonzero.s"
grep -F '  .word 3' "$build_dir/static_nested_record_nonzero.s" >/dev/null
grep -F '  .dword 1' "$build_dir/static_nested_record_nonzero.s" >/dev/null
grep -F '  .dword 2' "$build_dir/static_nested_record_nonzero.s" >/dev/null

"$minic" -S "$root/tests/programs/c0/static_zero_size_local_array.c" \
    -o "$build_dir/static_zero_size_local_array.s"
grep -E '^\.size __minic_static_local_[0-9_]+, 0$' \
    "$build_dir/static_zero_size_local_array.s" >/dev/null

cat >"$build_dir/static_flexible_array.c" <<'EOF'
struct StaticFlexibleArray {
    int tag;
    unsigned long tail[];
};
struct StaticFlexibleArray static_flexible_array = {
    .tag = 7,
    .tail = { [0 ... 2] = 11UL },
};
EOF
"$minic" -S "$build_dir/static_flexible_array.c" \
    -o "$build_dir/static_flexible_array.s"
grep -F '  .word 7' "$build_dir/static_flexible_array.s" >/dev/null
grep -F '.size static_flexible_array, 32' "$build_dir/static_flexible_array.s" >/dev/null
test "$(grep -c '  .dword 11' "$build_dir/static_flexible_array.s")" -eq 3

cat >"$build_dir/static_union_selection.c" <<'EOF'
struct CallbackNode {
    int count;
    union {
        void (*func)(unsigned long);
        void (*callback)(struct CallbackNode *);
    };
    int tail;
};
static void callback_node(struct CallbackNode *node) { (void)node; }
static struct CallbackNode callback_holder = {
    .count = 1,
    .callback = callback_node,
    .tail = 2,
};

struct AnonymousStructUnion {
    union {
        struct { void *a0; void *a1; };
        struct { unsigned long s0; unsigned long s1; };
    };
};
static struct AnonymousStructUnion anonymous_struct_union = { .s1 = 8UL };

static long backward_target;
struct BackwardUnion {
    int prefix;
    union {
        int *as_int;
        long *as_long;
    };
    int tail;
};
static struct BackwardUnion backward_union = {
    .tail = 3,
    .as_long = &backward_target,
};
EOF
"$minic" -S "$build_dir/static_union_selection.c" \
    -o "$build_dir/static_union_selection.s"
grep -F '  .dword callback_node' "$build_dir/static_union_selection.s" >/dev/null
grep -F '  .dword 8' "$build_dir/static_union_selection.s" >/dev/null
grep -F '  .dword backward_target' "$build_dir/static_union_selection.s" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_static_record_compound_literal_type.c" \
    -o "$build_dir/invalid.s" >"$build_dir/invalid.stdout" 2>"$build_dir/invalid.stderr"; then
    echo 'FAIL static aggregate discovery: mismatched record compound literal accepted' >&2
    exit 1
fi
grep -F 'static record compound literal type mismatch' "$build_dir/invalid.stderr" >/dev/null
printf '%s\n' \
    'PASS compiler/c0/static-aggregate-initializers compound-literal=record nested-nonzero=recursive zero-size-local-array=accepted static-fam=gnu-range+extended-storage union-selection=forward+anonymous-struct+backward-reloc designated-inner=shared mismatch=fail-closed'
