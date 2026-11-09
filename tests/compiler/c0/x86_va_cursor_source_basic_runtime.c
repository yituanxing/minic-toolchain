typedef __builtin_va_list MiniVa;

extern int vsnprintf(char *, unsigned long, const char *, MiniVa);

static int consume(MiniVa cursor) {
    return __builtin_va_arg(cursor, int);
}

static int from_source(unsigned int named, ...) {
    MiniVa cursor;
    MiniVa duplicate;
    char text[16];
    int result;
    __builtin_va_start(cursor, named);
    if (consume(cursor) != 71) return 1;
    __builtin_va_copy(duplicate, cursor);
    if (__builtin_va_arg(duplicate, int) != 82) return 2;
    if (__builtin_va_arg(cursor, int) != 82) return 3;
    result = vsnprintf(text, sizeof(text), "%d", cursor);
    __builtin_va_end(duplicate);
    __builtin_va_end(cursor);
    if (result != 2 || text[0] != '9' || text[1] != '3' || text[2] != 0)
        return 4;
    return 0;
}

int main(void) {
    MiniVa cursor;
    if (sizeof(cursor) != 24U || sizeof(MiniVa) != 24U) return 5;
    if ((char *)(&cursor + 1) - (char *)&cursor != 24) return 6;
    return from_source(1U, 71, 82, 93);
}
