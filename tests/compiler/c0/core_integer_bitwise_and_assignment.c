int core_m14_bitwise_and(int left, int right) {
    return left & right;
}

int core_m14_compound_int(int value, int right) {
    value &= right;
    return value;
}

unsigned char core_m14_compound_uchar(unsigned char value, unsigned int right) {
    value &= right;
    return value;
}

_Bool core_m14_compound_bool(_Bool value, _Bool right) {
    value &= right;
    return value;
}

int *core_m14_pick(int *value);
_Bool core_m14_report(int value);

int core_m14_single_lvalue(int *value, int right) {
    return (*core_m14_pick(value) &= right);
}

_Bool core_m14_linux_tail(int value) {
    _Bool ret = 1;
    ret &= core_m14_report(value);
    return ret;
}
