/* The zero-byte ABI return must follow Record identity through typedefs
 * and union tags, not rely on the spelling of `struct Empty`. */
union MC057_EmptyUnion {};
typedef union MC057_EmptyUnion MC057_EmptyAlias;

MC057_EmptyAlias mc057_union_direct(int choose) {
    MC057_EmptyAlias local = {};
    if (choose) return local;
    return (MC057_EmptyAlias){};
}

MC057_EmptyAlias mc057_union_forward(MC057_EmptyAlias value) {
    return value;
}

int mc057_union_caller(void) {
    MC057_EmptyAlias value = mc057_union_direct(1);
    mc057_union_forward(value);
    return 42;
}
