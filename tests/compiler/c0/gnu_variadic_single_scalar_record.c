typedef __builtin_va_list MiniVaList;

typedef struct {
    unsigned int value;
} MiniUserId;

typedef struct {
    unsigned int value;
} MiniGroupId;

typedef struct {
    double value;
} MiniReal;

typedef struct {
    void *value;
} MiniPointer;

unsigned int mini_va_record_sum(unsigned int tag, ...) {
    MiniVaList ap;
    __builtin_va_start(ap, tag);
    MiniUserId uid = __builtin_va_arg(ap, MiniUserId);
    MiniGroupId gid = __builtin_va_arg(ap, MiniGroupId);
    __builtin_va_end(ap);
    return uid.value + gid.value + tag;
}

unsigned int mini_va_record_many(unsigned int tag, ...) {
    MiniVaList ap;
    unsigned int total = tag;
    unsigned int i;
    __builtin_va_start(ap, tag);
    for (i = 0; i < 9; ++i) {
        MiniUserId current = __builtin_va_arg(ap, MiniUserId);
        total += current.value;
    }
    __builtin_va_end(ap);
    return total;
}

double mini_va_record_real(unsigned int tag, ...) {
    MiniVaList ap;
    double total = (double)tag;
    unsigned int i;
    __builtin_va_start(ap, tag);
    for (i = 0; i < 9; ++i) {
        MiniReal current = __builtin_va_arg(ap, MiniReal);
        total += current.value;
    }
    __builtin_va_end(ap);
    return total;
}

unsigned long mini_va_record_pointer(unsigned int tag, ...) {
    MiniVaList ap;
    __builtin_va_start(ap, tag);
    MiniPointer current = __builtin_va_arg(ap, MiniPointer);
    __builtin_va_end(ap);
    return (unsigned long)current.value + tag;
}
