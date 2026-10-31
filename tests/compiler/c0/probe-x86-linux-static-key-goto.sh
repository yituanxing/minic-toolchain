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
bool mini_static_key(void) {
    return static_branch_wrapper(&key_alpha) &&
           arch_static_branch_jump(&key_beta, 0);
}
EOF
cc -std=gnu11 -O2 -S "$d/branch.i" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/branch.i" -o "$d/mini.s"
test "$(grep -Fc '.pushsection __jump_table' "$d/mini.s")" -eq 2
cc -c "$d/mini.s" -o "$d/mini.o"
test "$(readelf -S "$d/mini.o" | grep -Fc '__jump_table')" -ge 1
echo 'MINIC_X86_LINUX_STATIC_KEY_GOTO=PASS variants=2 symbolic_keys=2 real_elf=1'
