struct OverAligned {
    char byte;
} __attribute__((__aligned__(16)));

_Static_assert(__alignof__(unsigned long) == 8, "gnu ulong alignment");
_Static_assert(__alignof(unsigned long long) == 8, "gnu alias alignment");
_Static_assert(_Alignof(struct OverAligned) == 16, "c11 record alignment");

unsigned long alignof_ulong(void) {
    return __alignof__(unsigned long);
}

unsigned long alignof_record(void) {
    return _Alignof(struct OverAligned);
}

int main(void) {
    return alignof_ulong() == 8 && alignof_record() == 16 ? 0 : 1;
}
