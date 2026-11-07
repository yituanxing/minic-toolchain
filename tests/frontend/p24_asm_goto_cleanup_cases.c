#ifndef P24_CASE
#define P24_CASE 0
#endif

static void p24_cleanup(int *value) { (void)value; }

#if P24_CASE == 0
int p24_asm_goto_same_scope(void) {
    asm goto("j %l[yes]\n\t.word %[ext]" : : [ext] "i"(33) : : yes);
    return 0;
yes:
    return 1;
}
#elif P24_CASE == 1
int p24_asm_goto_enter_cleanup(void) {
    asm goto("nop" : : : : inside);
    {
        int guard __attribute__((cleanup(p24_cleanup))) = 1;
inside:
        return guard;
    }
}
#elif P24_CASE == 2
int p24_asm_goto_leave_cleanup(void) {
    {
        int guard __attribute__((cleanup(p24_cleanup))) = 1;
        asm goto("nop" : : : : outside);
        return guard;
    }
outside:
    return 0;
}
#elif P24_CASE == 3
int p24_local_asm_goto_definition_context(void) {
    __label__ local;
    asm goto("nop" : : : : local);
    {
        int guard __attribute__((cleanup(p24_cleanup))) = 1;
local:
        return guard;
    }
}
#elif P24_CASE == 4
int p24_local_c_goto_definition_context(void) {
    __label__ local;
    goto local;
    {
        int guard __attribute__((cleanup(p24_cleanup))) = 1;
local:
        return guard;
    }
}
#elif P24_CASE == 5
int p24_backward_asm_goto_cross_cleanup(void) {
outside:
    {
        int guard __attribute__((cleanup(p24_cleanup))) = 1;
        asm goto("nop" : : : : outside);
        return guard;
    }
}
#elif P24_CASE == 6
int p24_local_asm_goto_same_scope(void) {
    __label__ local;
    asm goto("j %l[local]\n\t.word %[ext]" : : [ext] "i"(33) : : local);
    return 0;
local:
    return 1;
}
#elif P24_CASE == 7
int p24_backward_asm_goto_same_scope(void) {
local:
    asm goto("j %l[local]\n\t.word %[ext]" : : [ext] "i"(33) : : local);
    return 1;
}
#endif
