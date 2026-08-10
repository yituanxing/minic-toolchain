static int generic_probe(int value) {
    _Static_assert(__builtin_types_compatible_p(typeof(value), int), "block-scope type");
    return value;
}

_Static_assert(
    __builtin_types_compatible_p(typeof(generic_probe), typeof(generic_probe)) &&
        __builtin_types_compatible_p(typeof(1), int),
    "top-level " "type");

int main(void) {
    return generic_probe(7) == 7 ? 0 : 1;
}
