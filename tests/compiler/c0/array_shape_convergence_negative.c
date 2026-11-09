/* Exactly one ill-formed array contract per compile. GNU C and MiniC must
 * both reject it; no architecture may reinterpret it as a scalar object. */
#if CASE == 1
typedef int Row[3];
int bad(void)
{
    Row rows[2];
    rows[0] = (Row){1, 2, 3}; /* Arrays are not assignable. */
    return 0;
}
#elif CASE == 2
struct Flexible { int tag; int payload[]; };
unsigned long bad(void)
{
    return sizeof(((struct Flexible *)0)->payload); /* Incomplete type. */
}
#elif CASE == 3
int bad(void)
{
    int values[1.5]; /* An array bound must have integer type. */
    return values[0];
}
#elif CASE == 4
typedef int Row[3];
int bad(void)
{
    Row rows[2];
    int (*wrong)[2] = &rows[0]; /* Bound is part of pointee identity. */
    return (*wrong)[0];
}
#elif CASE == 5
struct Zero { int header; int empty[0]; };
struct Zero bad = { .header = 1, .empty = {7} }; /* No slots. */
#else
#error unknown array negative fixture CASE
#endif
