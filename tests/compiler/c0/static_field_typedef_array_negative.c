/* All cases are illegal under GNU C as well as the MiniC source policy. */
#if CASE == 1
typedef int Row3[3];
struct R { Row3 cells; };
static struct R item = { .cells = {[3] = 7} };
#elif CASE == 2
typedef int Row3[3];
struct R { Row3 cells; };
static struct R item = { .cells = {[0] = 1, [4] = 9} };
#elif CASE == 3
typedef int Row3[3];
struct R { Row3 cells; };
static struct R item = { .cells[0].missing = 1 };
#elif CASE == 4
typedef int Row3[3];
struct R { Row3 cells; };
static struct R item = { .invalid = {1, 2, 3} };
#elif CASE == 5
typedef int Row3[3];
struct R { Row3 cells; };
int bad(void) { struct R item = {{1, 2, 3}}; item.cells = item.cells; return 0; }
#elif CASE == 6
struct R { int flexible[]; int after; };
static struct R item = { .after = 3 };
#elif CASE == 7
typedef int Row3[3];
struct R { Row3 cells; };
static const struct R item = { .cells = {1, 2, 3} };
int bad(void) { item.cells[0] = 7; return 0; }
#elif CASE == 8
struct Z { int empty[0]; int following; };
static struct Z item = {{1}, 5}; /* Complete GNU zero array has no slots. */
#else
#error unknown static field typedef array negative CASE
#endif
