/* MC-002: nested record definition in a function-pointer field parameter
 * grows Program.records while the enclosing Record field is being parsed. */
struct R1 { int x; };
struct R2 { int x; };
struct R3 { int x; };
struct R4 { int x; };
struct R5 { int x; };
struct R6 { int x; };
struct R7 { int x; };
struct R8 { int x; };
struct R9 { int x; };
struct R10 { int x; };
struct R11 { int x; };
struct R12 { int x; };
struct R13 { int x; };
struct R14 { int x; };
#ifndef MC002_SKIP_R15
struct R15 { int x; };
#endif
#ifdef MC002_ADD_R16
struct R16 { int x; };
#endif

struct Outer {
    int (*callback)(struct Inner { int value; } *inner);
};

#ifdef MC002_DUPLICATE_FIELD
struct DuplicateOuter {
    int (*callback)(struct InnerA { int value; } *inner);
    int (*callback)(struct InnerB { int value; } *inner);
};
#endif

int main(void) {
    return sizeof(struct Outer) >= sizeof(void *) ? 7 : 1;
}
