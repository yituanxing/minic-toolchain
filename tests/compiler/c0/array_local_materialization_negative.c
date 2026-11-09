/* One independent ill-formed declaration/expression for each CASE. */
#if CASE == 1
void bad(void) { int values[3]; values[0] = 1; values = values; }
#elif CASE == 2
int bad(void) { int values[1.5]; return values[0]; }
#elif CASE == 3
int bad(void) { const int values[2] = {1, 2}; values[1] = 3; return 0; }
#elif CASE == 4
int bad(void)[3];
#elif CASE == 5
int functions[2](void);
#elif CASE == 6
int length;
int values[length];
#elif CASE == 7
void bad[2];
#elif CASE == 8
struct Bad { int flexible[]; int tail; };
#elif CASE == 9
union Bad { int flexible[]; int tail; };
#elif CASE == 10
int bad(void) { int values[2]; int values[3]; return 0; }
#elif CASE == 11
int bad(void) { int values[2] = 7; return values[0]; }
#elif CASE == 12
int bad(void) { int values[2] = {1, 2, 3}; return values[0]; }
#elif CASE == 13
int bad(void) { int values[3]; int (*wrong)[2] = &values; return (*wrong)[0]; }
#elif CASE == 14
typedef int Incomplete[];
unsigned long bad(void) { return sizeof(Incomplete); }
#elif CASE == 15
int bad(void) { char values[2]; values = "a"; return 0; }
#elif CASE == 16
struct R { int cells[3]; };
int bad(void) {
    const struct R value = {{1, 2, 3}};
    value.cells[0] = 7; /* Record lvalue's const must propagate to array elements. */
    return 0;
}
#else
#error unknown local array negative CASE
#endif
