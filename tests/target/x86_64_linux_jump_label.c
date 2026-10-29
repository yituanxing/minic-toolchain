/* SPDX-License-Identifier: GPL-2.0 */
/* Linux x86 jump_label.h: GNU asm goto must retain the patch-site table,
 * both PREL32 entries, and the key+flags offset in the 8-byte third word. */
struct linux_static_key { unsigned long enabled; };
static struct linux_static_key branch_key;

int linux_static_branch_tagged(void) {
    asm goto("1:"
             "jmp %l[l_yes] # objtool NOPs this \n\t"
             ".pushsection __jump_table,  \"aw\" \n\t"
             " " ".balign 8" " " "\n\t"
             ".long 1b - . \n\t"
             ".long %l[l_yes] - . \n\t"
             " " ".quad" " " "%c0 + %c1 - .\n\t"
             ".popsection \n\t"
             : : "i" (&branch_key), "i" (2) : : l_yes);
    return 0;
l_yes:
    return 1;
}

int linux_static_branch_plain(void) {
    asm goto("1:"
             "jmp %l[l_yes]\n\t"
             ".pushsection __jump_table,  \"aw\" \n\t"
             " " ".balign 8" " " "\n\t"
             ".long 1b - . \n\t"
             ".long %l[l_yes] - . \n\t"
             " " ".quad" " " "%c0 + %c1 - .\n\t"
             ".popsection \n\t"
             : : "i" (&branch_key), "i" (1) : : l_yes);
    return 0;
l_yes:
    return 1;
}

/* Source-inline GNU operands are not necessarily constants until the
 * caller's always_inline symbolic/integer specialization has occurred.
 * This is the same producer shape used by the real Linux `arch_static_branch`
 * after preprocessor expansion; if lowered as an SSA pointer, GNU AS cannot
 * encode the required __jump_table third entry. */
static inline __attribute__((always_inline)) int
linux_inline_key_with_branch(struct linux_static_key *key, int branch) {
    asm goto("1:"
             "jmp %l[l_yes] # objtool NOPs this \n\t"
             ".pushsection __jump_table,  \"aw\" \n\t"
             " " ".balign 8" " " "\n\t"
             ".long 1b - . \n\t"
             ".long %l[l_yes] - . \n\t"
             " " ".quad" " " "%c0 + %c1 - .\n\t"
             ".popsection \n\t"
             : : "i" (key), "i" (2 | branch) : : l_yes);
    return 0;
l_yes:
    return 1;
}

int linux_static_branch_specialized(void) {
    return linux_inline_key_with_branch(&branch_key, 1);
}
