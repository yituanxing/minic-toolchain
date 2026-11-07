struct S { long a[3]; };
struct S produce(void);

void invalid(void) {
    long (*p)[3] = &produce().a;
    (void)p;
}
