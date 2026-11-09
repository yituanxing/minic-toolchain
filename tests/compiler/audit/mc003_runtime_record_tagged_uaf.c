/* MC-003 original shape: a tagged Record is created in a positional
 * initializer expression. Record arena capacity varies by target, so phase
 * the number of preceding records to exercise grow and no-grow paths. */
struct R1 { int a; };
struct R2 { int a; };
struct R3 { int a; };
struct R4 { int a; };
struct R5 { int a; };
struct R6 { int a; };
struct R7 { int a; };
struct R8 { int a; };
struct R9 { int a; };
struct R10 { int a; };
struct R11 { int a; };
struct R12 { int a; };
struct R13 { int a; };
struct R14 { int a; };
#ifndef MINIC_SKIP_R15
struct R15 { int a; };
#endif
#ifdef MINIC_ADD_R16
struct R16 { int a; };
#endif

struct Outer { int first; int second; };

int main(void) {
    struct Outer outer = { sizeof(struct Inner { int z; }), 22 };
    return outer.first + outer.second;
}
