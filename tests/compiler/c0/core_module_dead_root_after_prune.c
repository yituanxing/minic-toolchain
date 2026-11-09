/* P32 negative: source GNU symbol metadata is not a liveness root by itself.
 * Each helper is referenced only from an if (0) Core edge. A second module
 * root pass must NOT resurrect the helper and its undefined external call. */
extern int mc_dead_section_unresolved(void);
extern int mc_dead_asmname_unresolved(void);

static int __attribute__((section(".text.mc_dead_section")))
mc_dead_section_helper(void) {
    return mc_dead_section_unresolved();
}

static int mc_dead_asmname_helper(void) __asm__("mc_dead_asmname_symbol");
static int mc_dead_asmname_helper(void) {
    return mc_dead_asmname_unresolved();
}

int main(void) {
    if (0)
        return mc_dead_section_helper() + mc_dead_asmname_helper();
    return 0;
}
