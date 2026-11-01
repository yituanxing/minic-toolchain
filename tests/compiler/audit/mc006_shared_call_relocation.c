/* MC-006: one inline source CALL is instantiated under two different
 * symbolic caller facts. The emitted ELF must retain both key relocations. */
typedef _Bool bool;
struct static_key { unsigned long enabled; };
static struct static_key keys[2];

__attribute__((always_inline)) static inline bool
leaf(struct static_key *key) {
    asm goto("1:"
        "jmp %l[l_yes] # objtool NOPs this \n\t"
        ".pushsection __jump_table,  \"aw\" \n\t" " " ".balign 8" " " "\n\t"
        ".long 1b - . \n\t" ".long %l[l_yes] - . \n\t" " "
        ".quad" " " "%c0 + %c1 - .\n\t" ".popsection \n\t"
        : : "i"(key), "i"(2) : : l_yes);
    return 0;
l_yes:
    return 1;
}

__attribute__((always_inline)) static inline bool
mid(struct static_key *key) {
    return leaf(key);
}

#ifdef MC006_REVERSE
int beta(void) { return mid(&keys[1]); }
int alpha(void) { return mid(&keys[0]); }
#else
int alpha(void) { return mid(&keys[0]); }
int beta(void) { return mid(&keys[1]); }
#endif

int main(void) { return alpha() == 1 && beta() == 1 ? 0 : 1; }
