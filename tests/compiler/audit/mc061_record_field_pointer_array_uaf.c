/* MC-061: a pointer-to-array field's sizeof(anonymous struct) may grow
 * Program.records before the outer field duplicate-name check resumes.
 * Capacity: default 15 records hits RV32/RV64/Loong; skip R15 hits x86/A64. */
struct R1 { int v; };
struct R2 { int v; };
struct R3 { int v; };
struct R4 { int v; };
struct R5 { int v; };
struct R6 { int v; };
struct R7 { int v; };
struct R8 { int v; };
struct R9 { int v; };
struct R10 { int v; };
struct R11 { int v; };
struct R12 { int v; };
struct R13 { int v; };
struct R14 { int v; };
#ifndef MINIC_SKIP_R15
struct R15 { int v; };
#endif
#ifdef MINIC_ADD_R16
struct R16 { int v; };
#endif
struct Outer { int (*field)[sizeof(struct { int z; })]; };
int main(void) {
    struct Outer object;
    (void)object;
    return (int)sizeof(((struct Outer *)0)->field[0]) - 4;
}
