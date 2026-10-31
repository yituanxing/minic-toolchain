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
static struct CallbackNode callback_holder __attribute__((used)) = {
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
static struct AnonymousStructUnion anonymous_struct_union __attribute__((used)) = { .s1 = 8UL };

static long backward_target;
struct BackwardUnion {
    int prefix;
    union {
        int *as_int;
        long *as_long;
    };
    int tail;
};
static struct BackwardUnion backward_union __attribute__((used)) = {
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
# RV64's flat record and pointer relocation array now share the common
# DataLayout slot walk, while preserving .word/.half/.dword GNU directives.
cat > "$build_dir/static_flat_reloc_shared.c" <<'EOF'
static int record_target[4] = {2, 3, 5, 7};
struct FlatReloc { int count; int *ptr; unsigned short flags; };
static struct FlatReloc shared = {17, &record_target[2], 23};
static int *const pointers[4] = {
    &record_target[1], 0, &record_target[3], &record_target[0]
};
int main(void) {
    return shared.count == 17 && shared.ptr == &record_target[2] &&
           shared.flags == 23 && pointers[0] == &record_target[1] &&
           pointers[1] == 0 && pointers[2] == &record_target[3] &&
           pointers[3] == &record_target[0] ? 42 : 1;
}
EOF
"$minic" -S "$build_dir/static_flat_reloc_shared.c" \
    -o "$build_dir/static_flat_reloc_shared.s"
grep -F '  .word 17' "$build_dir/static_flat_reloc_shared.s" >/dev/null
grep -F '  .dword record_target+8' "$build_dir/static_flat_reloc_shared.s" >/dev/null
grep -F '  .half 23' "$build_dir/static_flat_reloc_shared.s" >/dev/null
grep -F '  .dword record_target+12' "$build_dir/static_flat_reloc_shared.s" >/dev/null

# Force the RV64 record-array path through shared recursive DataLayout.
# Assert relocation addends and target-specific .dword spelling, while the
# existing tests cover bitfield/flexible fallback compatibility.
cat > "$build_dir/static_nested_shared_layout.c" <<'EOF'
static int members_target[4] = {3, 5, 7, 11};
struct Inner { unsigned short count; int *pointer; };
struct Outer { struct Inner members[2]; unsigned long total; };
static struct Outer shared_nested = {
    {{17, &members_target[1]}, {29, &members_target[3]}}, 41
};
int main(void) {
    return shared_nested.members[0].count == 17 &&
           shared_nested.members[0].pointer == &members_target[1] &&
           shared_nested.members[1].count == 29 &&
           shared_nested.members[1].pointer == &members_target[3] &&
           shared_nested.total == 41 ? 42 : 1;
}
EOF
"$minic" -S "$build_dir/static_nested_shared_layout.c" \
    -o "$build_dir/static_nested_shared_layout.s"
grep -F '  .dword members_target+4' "$build_dir/static_nested_shared_layout.s" >/dev/null
grep -F '  .dword members_target+12' "$build_dir/static_nested_shared_layout.s" >/dev/null
# Bitfield + relocation combinations used to bypass the shared recursive
# walker entirely. Verify the same source against GNU GCC and MiniC under
# QEMU; the aggregate is intentionally nested, has a zero-width barrier and
# a pointer relocation following a bitfield run.
cat > "$build_dir/static_bitfield_shared.c" <<'EOF'
static int target_words[5] = {2, 3, 5, 7, 11};
struct Bits {
    unsigned int a:6;
    unsigned int b:10;
    unsigned int :0;
    unsigned int c:13;
    unsigned int d:5;
    int *pointer;
};
struct Aggregate {
    struct Bits bits[2];
    long tail;
};
static struct Aggregate shared_bits = {
    {{17, 301, 4119, 19, &target_words[2]},
     {11, 203, 33, 9, &target_words[4]}},
    41
};
int main(void) {
    return shared_bits.bits[0].a == 17 &&
           shared_bits.bits[0].b == 301 &&
           shared_bits.bits[0].c == 4119 &&
           shared_bits.bits[0].d == 19 &&
           shared_bits.bits[0].pointer == &target_words[2] &&
           shared_bits.bits[1].a == 11 &&
           shared_bits.bits[1].b == 203 &&
           shared_bits.bits[1].c == 33 &&
           shared_bits.bits[1].d == 9 &&
           shared_bits.bits[1].pointer == &target_words[4] &&
           shared_bits.tail == 41 ? 42 : 1;
}
EOF
"$minic" -S "$build_dir/static_bitfield_shared.c" \
    -o "$build_dir/static_bitfield_shared.s"
grep -F '.dword target_words+8' "$build_dir/static_bitfield_shared.s" >/dev/null
grep -F '.dword target_words+16' "$build_dir/static_bitfield_shared.s" >/dev/null
if [ -n "${RISCV_CC:-}" ] && [ -n "${QEMU_RISCV64:-}" ]; then
    "$RISCV_CC" -static -no-pie -o "$build_dir/static_bitfield_shared.minic" \
        "$build_dir/static_bitfield_shared.s"
    "$RISCV_CC" -static -no-pie -o "$build_dir/static_bitfield_shared.gcc" \
        "$build_dir/static_bitfield_shared.c"
    for compiler in minic gcc; do
        set +e
        "$QEMU_RISCV64" "$build_dir/static_bitfield_shared.$compiler"
        actual=$?
        set -e
        if [ "$actual" -ne 42 ]; then
            echo "FAIL recursive bitfield GlobalObject GNU-QEMU compiler=$compiler exit=$actual" >&2
            exit 1
        fi
    done
fi

# RV64 previously used its own complete recursive serializer for these
# shapes.  Both GNU and MiniC must agree in real QEMU execution, including
# relocated pointers after bitfields and a dynamically extended GNU FAM.
cat > "$build_dir/static_mixed_flexible_shared.c" <<'EOF'
static long pointed[4] = {3, 5, 7, 11};
struct Mix {
    unsigned int a:6;
    unsigned int b:10;
    long *address;
    unsigned long tail[];
};
static struct Mix mix = {
    .a = 17, .b = 301, .address = &pointed[2],
    .tail = { [0 ... 3] = 11UL }
};
int main(void) {
    return mix.a == 17 && mix.b == 301 &&
           mix.address == &pointed[2] &&
           mix.tail[0] == 11 && mix.tail[1] == 11 &&
           mix.tail[2] == 11 && mix.tail[3] == 11 ? 42 : 1;
}
EOF
"$minic" -S "$build_dir/static_mixed_flexible_shared.c" \
    -o "$build_dir/static_mixed_flexible_shared.s"
grep -F '.dword pointed+16' "$build_dir/static_mixed_flexible_shared.s" >/dev/null
grep -F '.size mix, 48' "$build_dir/static_mixed_flexible_shared.s" >/dev/null
test "$(grep -c '  .dword 11' "$build_dir/static_mixed_flexible_shared.s")" -ge 4

# Zero-sized GNU fields and nested complete empty records consume no
# flattened slots, yet must preserve the offsets of later stored members.
cat > "$build_dir/static_empty_nested_shared.c" <<'EOF'
struct Empty {};
struct Mixed {
    unsigned int prefix;
    struct Empty none;
    unsigned char missing[0];
    unsigned long last;
};
static struct Mixed sparse = { .prefix = 7, .last = 41 };
int main(void) {
    return sparse.prefix == 7 && sparse.last == 41 ? 42 : 1;
}
EOF
"$minic" -S "$build_dir/static_empty_nested_shared.c" \
    -o "$build_dir/static_empty_nested_shared.s"
grep -F '  .dword 41' "$build_dir/static_empty_nested_shared.s" >/dev/null

if [ -n "${RISCV_CC:-}" ] && [ -n "${QEMU_RISCV64:-}" ]; then
    for case_name in static_mixed_flexible_shared static_empty_nested_shared; do
        "$RISCV_CC" -static -no-pie \
            -o "$build_dir/$case_name.minic" "$build_dir/$case_name.s"
        "$RISCV_CC" -static -no-pie \
            -o "$build_dir/$case_name.gcc" "$build_dir/$case_name.c"
        for compiler in minic gcc; do
            set +e
            "$QEMU_RISCV64" "$build_dir/$case_name.$compiler"
            status=$?
            set -e
            if [ "$status" -ne 42 ]; then
                echo "FAIL shared GlobalObject GNU-QEMU case=$case_name compiler=$compiler exit=$status" >&2
                exit 1
            fi
        done
    done
fi
printf '%s\n' \
    'PASS compiler/c0/static-aggregate-initializers compound-literal=record nested-nonzero=recursive zero-size-local-array=accepted static-fam=gnu-range+extended-storage union-selection=forward+anonymous-struct+backward-reloc designated-inner=shared flat-record=shared-rv64-layout nested=shared-recursive bitfields=shared-packed flex=shared-recursive empty=zero-slot mismatch=fail-closed'
