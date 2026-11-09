/* P44: only an *emitted* GNU inline ASM operand can keep its static
 * relocation target alive. A dead C function or dead Core CFG branch is
 * not a linker-visible root merely because the parser saw an "i" operand.
 * The live producer must retain its static function and object on every
 * admitted target, including in a GNU-linked x86 ELF executable. */
extern int p44_dead_unresolved(void);

static int p44_dead_fn(void) __attribute__((unused));
static int p44_dead_fn(void) {
    return p44_dead_unresolved();
}

static int p44_dead_object __attribute__((unused)) = 17;

static void p44_dead_asm_owner(void) __attribute__((unused));
static void p44_dead_asm_owner(void) {
    __asm__ volatile(".pushsection .rodata\n\t"
                     ".quad %c0\n\t"
                     ".quad %c1\n\t"
                     ".popsection" : :
                     "i"(p44_dead_fn), "i"(&p44_dead_object));
}

static int p44_dead_branch_fn(void) __attribute__((unused));
static int p44_dead_branch_fn(void) {
    return p44_dead_unresolved();
}

static int p44_live_fn(void) __attribute__((unused));
static int p44_live_fn(void) {
    return 29;
}

static int p44_live_object __attribute__((unused)) = 31;
static unsigned char p44_live_bytes[16] __attribute__((unused));

int main(void) {
    if (0) {
        __asm__ volatile(".pushsection .rodata\n\t"
                         ".quad %c0\n\t"
                         ".popsection" : : "i"(p44_dead_branch_fn));
    }
    __asm__ volatile(".pushsection .rodata\n\t"
                     ".quad %c0\n\t"
                     ".quad %c1\n\t"
                     ".popsection" : :
                     "i"(p44_live_fn), "i"(&p44_live_object));
    /* The source-symbol edge must survive both simple byte offsets and
     * nested expression wrappers; an ASM operand is not necessarily a
     * direct FUNCTION/GLOBAL_OBJECT AST node. */
    __asm__ volatile(".pushsection .rodata\n\t"
                     ".quad %c0\n\t"
                     ".quad %c1\n\t"
                     ".popsection" : :
                     "i"((char *)&p44_live_bytes + 4),
                     "i"(((char *)&p44_live_bytes[0]) + (2 + 6)));
    return 0;
}
