/* Both GNU -pedantic-errors and MiniC must reject the extra fourth value. */
struct Mc038Overflow { struct { int a; int b; }; int c; };
int mc038_invalid(void) {
    struct Mc038Overflow x = { .b = 2, 3, 4 };
    return x.c;
}
