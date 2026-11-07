/* MC-023: valid GNU C; ASan detects Program.records UAF in backward overwrite. */
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
struct Inner { int p; int q; };
struct Outer { struct Inner inner; int t; };
#ifdef MINIC_NO_RECORD_GROW
#define MC023_FIRST 4
#else
#define MC023_FIRST sizeof(struct {int z;})
#endif
static struct Outer value = { .inner = {0}, .inner = {MC023_FIRST, 3} };
int main(void) { return value.inner.p + value.inner.q; }
