extern int generic_side_effect(void);

struct TypeofPending;
extern __attribute__((section(".probe.typeof.incomplete")))
    __typeof__(struct TypeofPending) typeof_pending_object;

void *typeof_incomplete_object_address(void) {
    return &typeof_pending_object;
}

unsigned long generic_selected_value(unsigned long value) {
    return _Generic(value,
                    int: 3UL,
                    unsigned long: 7UL,
                    default: 9UL);
}

unsigned long generic_default_value(void *value) {
    return _Generic(value,
                    int: 3UL,
                    unsigned long: 7UL,
                    default: 9UL);
}

unsigned long generic_controlling_is_unevaluated(void) {
    return _Generic(generic_side_effect(),
                    int: 11UL,
                    default: 13UL);
}

unsigned long typeof_expression_size(unsigned long *value) {
    return sizeof(typeof(*value));
}

unsigned long typeof_type_name_size(void) {
    return sizeof(__typeof__(unsigned long));
}

unsigned long typeof_generic_pointer_size(const void *addr) {
    return sizeof(const volatile typeof(
        _Generic((*(unsigned long *)addr),
                 char: (char)0,
                 unsigned char: (unsigned char)0,
                 signed char: (signed char)0,
                 unsigned short: (unsigned short)0,
                 signed short: (signed short)0,
                 unsigned int: (unsigned int)0,
                 signed int: (signed int)0,
                 unsigned long: (unsigned long)0,
                 signed long: (signed long)0,
                 unsigned long long: (unsigned long long)0,
                 signed long long: (signed long long)0,
                 default: (*(unsigned long *)addr))) *);
}
