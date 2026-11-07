/* P44: GNU `i` must preserve a known pointer-symbol fact even when a distinct
 * read/write ASM output escapes the pointer parameter's lvalue. The source
 * FunctionBody is shared by inline variants; producing the symbolic text
 * before invalidating mutable source locals is essential. */
static int p44_mutating_symbolic_slots[8] = { 11, 22, 33, 44 };

#ifdef P44_DIRECT_OUTPUT_LOCAL
#define P44_OUTPUT_LVALUE(p) (p)
#else
#define P44_OUTPUT_LVALUE(p) (*(&(p)))
#endif

static __attribute__((always_inline)) inline int
p44_inline_mutating_symbolic(int *p) {
    __asm__ volatile(".pushsection .rodata\n\t"
                     ".quad %c1\n\t"
                     ".popsection"
                     : "+r"(P44_OUTPUT_LVALUE(p)) : "i"(p));
    return 0;
}

int main(void) {
    return p44_inline_mutating_symbolic(p44_mutating_symbolic_slots);
}
