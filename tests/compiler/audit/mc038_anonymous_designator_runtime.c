/* GNU C runtime designator continuation through anonymous record fields.
 * Compile this TU with MiniC; the caller/oracle TU with GNU cc. */
struct Mc038Simple {
    struct { int a; int b; };
    int c;
};

struct Mc038Deep {
    struct { struct { int a; int b; }; int hidden; };
    int c;
};

struct Mc038Union {
    union { int a; int b; };
    int c;
};

struct Mc038Offset {
    int z;
    int y;
    struct { int a; int b; };
    int c;
};

struct Mc038Named {
    struct { int a; int b; } inner;
    int c;
};

struct Mc038Multiple {
    struct { int a; int b; };
    int c;
    int d;
};

struct Mc038Preserve {
    struct { int a; int b; int c; };
    int d;
};

int mc038_simple(void) {
    struct Mc038Simple x = { .b = 2, 3 };
    return x.a * 100 + x.b * 10 + x.c;
}
int mc038_deep(void) {
    struct Mc038Deep x = { .b = 2, 3 };
    return x.a * 1000 + x.b * 10 + x.hidden * 100 + x.c;
}
int mc038_union(void) {
    struct Mc038Union x = { .b = 2, 3 };
    return x.b * 10 + x.c;
}
int mc038_offset(void) {
    struct Mc038Offset x = { .b = 2, 3 };
    return x.z * 1000 + x.y * 100 + x.a * 100 + x.b * 10 + x.c;
}
int mc038_named(void) {
    struct Mc038Named x = { .inner.b = 2, 3 };
    return x.inner.a * 100 + x.inner.b * 10 + x.c;
}
int mc038_named_mid(void) {
    struct Mc038Named x = { .inner.a = 2, 3 };
    return x.inner.a * 10 + x.inner.b + x.c * 100;
}
int mc038_deep_multi(void) {
    struct Mc038Deep x = { .b = 2, 3, 4 };
    return x.b * 10 + x.hidden * 100 + x.c;
}
int mc038_named_multi(void) {
    struct Mc038Named x = { .inner.a = 2, 3, 4 };
    return x.inner.a * 100 + x.inner.b * 10 + x.c;
}
int mc038_dotted_after_positional(void) {
    struct Mc038Named x = { .inner.a = 1, 2, .c = 3 };
    return x.inner.a * 100 + x.inner.b * 10 + x.c;
}
int mc038_preserve_designated(void) {
    struct Mc038Preserve x = { .c = 7, .a = 2, 3 };
    return x.a * 1000 + x.b * 100 + x.c * 10 + x.d;
}
int mc038_preserve_outer(void) {
    struct Mc038Named x = { .c = 7, .inner.a = 2, 3 };
    return x.inner.a * 100 + x.inner.b * 10 + x.c;
}
int mc038_multiple(void) {
    struct Mc038Multiple x = { .b = 2, 3, 4 };
    return x.b * 100 + x.c * 10 + x.d;
}
int mc038_explicit(void) {
    struct Mc038Simple x = { .b = 2, .c = 3 };
    return x.b * 10 + x.c;
}
