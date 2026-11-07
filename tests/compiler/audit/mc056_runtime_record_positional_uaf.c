/* MC-056: a nested anonymous Record grows Program.records mid-positional parse.
 * Use GNU -E -P first. Capacity phases differ by target: default 15 types
 * hits RV32/RV64/Loong, -DMINIC_SKIP_R15 hits x86/AArch64. */
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
struct Pair { int first; int second; };
int main(void) {
    struct Pair pair = { sizeof(struct { int z; }), 3 };
    return pair.first + pair.second;
}
