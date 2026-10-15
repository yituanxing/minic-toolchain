struct Inner {
    int x;
    int y;
};

struct Outer {
    struct Inner inner;
    int z;
};

int chained_record_designator(void) {
    struct Outer value = {
        .inner.x = 3,
        .inner.y = 4,
        .z = 5,
    };

    return value.inner.x + value.inner.y + value.z;
}
