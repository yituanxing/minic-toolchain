/* P41 regression: member cv-qualification is a source-type property. */
struct P41Counter {
    volatile unsigned int embedded;
    unsigned int plain;
};

struct P41Outer {
    struct P41Counter child;
    unsigned int *pointer;
    unsigned int array[2];
    unsigned int bits:5;
};

volatile struct P41Counter p41_global;

unsigned int p41_read_global_twice(void) {
    return p41_global.plain + p41_global.plain;
}

unsigned int p41_read_pointer_twice(volatile struct P41Counter *pointer) {
    return pointer->plain + pointer->plain;
}

unsigned int p41_read_nested_pointer_twice(volatile struct P41Counter *pointer) {
    return pointer->embedded + pointer->plain;
}

void p41_store_pointer(volatile struct P41Counter *pointer, unsigned int value) {
    pointer->plain = value;
}

void p41_discard_volatile_member(volatile struct P41Counter *pointer) {
    pointer->plain;
}

int p41_volatile_member_generic(volatile struct P41Counter *pointer) {
    return _Generic(&pointer->plain, volatile unsigned int *: 1, default: 0);
}

int p41_nested_record_generic(volatile struct P41Outer *outer) {
    return _Generic(&outer->child.plain, volatile unsigned int *: 1, default: 0);
}

int p41_pointer_field_generic(volatile struct P41Outer *outer) {
    return _Generic(&outer->pointer, unsigned int *volatile *: 1, default: 0);
}

int p41_array_element_generic(volatile struct P41Outer *outer) {
    return _Generic(&outer->array[0], volatile unsigned int *: 1, default: 0);
}

int p41_const_volatile_member_generic(const volatile struct P41Counter *pointer) {
    return _Generic(&pointer->plain, const volatile unsigned int *: 1, default: 0);
}

unsigned int p41_read_bitfield(volatile struct P41Outer *outer) {
    return outer->bits;
}

#ifdef P41_NATIVE_MAIN
int main(void) {
    volatile struct P41Counter counter;
    volatile struct P41Outer outer;
    counter.plain = 3;
    counter.embedded = 9;
    outer.bits = 17;
    outer.child.plain = 7;
    outer.array[0] = 11;
    if (p41_read_pointer_twice(&counter) != 6U ||
        p41_read_nested_pointer_twice(&counter) != 12U ||
        p41_volatile_member_generic(&counter) != 1 ||
        p41_nested_record_generic(&outer) != 1 ||
        p41_pointer_field_generic(&outer) != 1 ||
        p41_array_element_generic(&outer) != 1 ||
        p41_const_volatile_member_generic(&counter) != 1 ||
        p41_read_bitfield(&outer) != 17U) {
        return 1;
    }
    p41_store_pointer(&counter, 5U);
    return counter.plain == 5U ? 0 : 2;
}
#endif
