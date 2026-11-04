#!/usr/bin/env bash
# Exact Linux 6.6 arch_static_branch/arch_static_branch_jump GNU asm-goto:
# patch-site JMP and addressable __jump_table ELF metadata use a symbolic
# "i" static key, a compile-time branch bit and a C destination label.
set -Eeuo pipefail
minic=$1
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat >"$d/branch.i" <<'EOF'
typedef _Bool bool;
enum { false = 0, true = 1 };
struct static_key { unsigned long enabled; };
extern struct static_key key_alpha, key_beta;
static inline __attribute__((always_inline)) bool
arch_static_branch(struct static_key *key, bool branch) {
    asm goto("1:"
        "jmp %l[l_yes] # objtool NOPs this \n\t"
        ".pushsection __jump_table,  \"aw\" \n\t" " " ".balign 8" " " "\n\t"
        ".long 1b - . \n\t" ".long %l[l_yes] - . \n\t" " "
        ".quad" " " "%c0 + %c1 - .\n\t" ".popsection \n\t"
        : : "i"(key), "i"(2 | branch) : : l_yes);
    return 0;
l_yes:
    return 1;
}
static inline __attribute__((always_inline)) bool
arch_static_branch_jump(struct static_key *key, bool branch) {
    asm goto("1:"
        "jmp %l[l_yes]\n\t"
        ".pushsection __jump_table,  \"aw\" \n\t" " " ".balign 8" " " "\n\t"
        ".long 1b - . \n\t" ".long %l[l_yes] - . \n\t" " "
        ".quad" " " "%c0 + %c1 - .\n\t" ".popsection \n\t"
        : : "i"(key), "i"(branch) : : l_yes);
    return 0;
l_yes:
    return 1;
}
static inline __attribute__((always_inline)) bool
static_branch_wrapper(struct static_key *key) {
    return arch_static_branch(key, 1);
}
static inline __attribute__((always_inline)) bool
static_key_false(struct static_key *key) {
    return arch_static_branch(key, false);
}
static inline __attribute__((always_inline)) bool
static_key_true(struct static_key *key) {
    return !arch_static_branch(key, true);
}
bool mini_static_key(void) {
    return static_branch_wrapper(&key_alpha) &&
           arch_static_branch_jump(&key_beta, 0) &&
           static_key_false(&key_alpha) &&
           static_key_true(&key_beta);
}
EOF
cc -std=gnu11 -O2 -S "$d/branch.i" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/branch.i" -o "$d/mini.s"
test "$(grep -Fc '.pushsection __jump_table' "$d/mini.s")" -eq 4
cc -c "$d/mini.s" -o "$d/mini.o"
test "$(readelf -S "$d/mini.o" | grep -Fc '__jump_table')" -ge 1
test "$(readelf -Wr "$d/mini.o" | grep -Ec 'R_X86_64_PC64.*key_(alpha|beta)')" -eq 4
readelf -Wr "$d/mini.o" >"$d/relocations.txt"
grep -Eq "Relocation section '.rela__jump_table'.*contains 12 entries:" \
    "$d/relocations.txt"
# The key address and the branch bit are both ELF immediate relocations.
# Two calls to the same key with opposite branch defaults must retain both
# addends rather than accidentally reusing a shared CALL's first clone.
grep -Eq 'R_X86_64_PC64.*key_alpha \+ 2([[:space:]]|$)' "$d/relocations.txt"
grep -Eq 'R_X86_64_PC64.*key_alpha \+ 3([[:space:]]|$)' "$d/relocations.txt"
grep -Eq 'R_X86_64_PC64.*key_beta \+ 3([[:space:]]|$)' "$d/relocations.txt"

# A dynamic pointer is not a GNU `i` immediate, even if the branch is a
# literal. This rejects the unspecialized helper and prevents silently
# reinterpreting the operand as a runtime register or wrong relocation.
cat >"$d/dynamic-negative.i" <<'EOF'
typedef _Bool bool;
enum { false = 0, true = 1 };
struct static_key { unsigned long enabled; };
static inline __attribute__((always_inline)) bool
arch_static_branch(struct static_key *key, bool branch) {
    asm goto("1: jmp %l[l_yes]\n\t"
             ".pushsection __jump_table, \"aw\"\n\t"
             ".balign 8\n\t"
             ".long 1b - .\n\t"
             ".long %l[l_yes] - .\n\t"
             ".quad %c0 + %c1 - .\n\t"
             ".popsection\n\t"
             : : "i"(key), "i"(2 | branch) : : l_yes);
    return false;
l_yes:
    return true;
}
bool mini_dynamic_key(struct static_key *key) {
    return arch_static_branch(key, false);
}
EOF
if cc -std=gnu11 -O2 -S "$d/dynamic-negative.i" -o "$d/gnu-negative.s" \
        >"$d/gnu-negative.log" 2>&1; then
    echo 'MINIC_X86_STATIC_KEY_DYNAMIC_GNU_UNEXPECTED_ACCEPT' >&2
    exit 1
fi
if "$minic" --target=x86_64-linux -S "$d/dynamic-negative.i" \
        -o "$d/mini-negative.s" >"$d/mini-negative.log" 2>&1; then
    echo 'MINIC_X86_STATIC_KEY_DYNAMIC_UNEXPECTED_ACCEPT' >&2
    exit 1
fi
if ! grep -Eq 'CORE_ASM_GOTO_IMMEDIATE_FAILURE|Core IR does not yet support' \
        "$d/mini-negative.log"; then
    tail -n 12 "$d/mini-negative.log" >&2
    exit 1
fi
echo 'MINIC_X86_LINUX_STATIC_KEY_GOTO=PASS variants=4 symbolic_keys=2 real_elf=1 dynamic_immediate_rejected=1'
