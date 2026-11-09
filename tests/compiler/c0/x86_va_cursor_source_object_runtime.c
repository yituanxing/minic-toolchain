/* x86-64 SysV source-object vs internal vararg-tag regression.
 * GNU __builtin_va_list is an array[1] of a 24-byte struct, not a pointer.
 * Run with host GCC and MiniC native; the latter must match GCC or reject
 * explicitly while the real source representation is being introduced. */
typedef __builtin_va_list MiniVa;

extern int vsnprintf(char *, unsigned long, const char *, MiniVa);

struct EmbeddedVa {
    unsigned char prefix;
    MiniVa cursor;
    unsigned char suffix;
};

static MiniVa global_cursor;
static MiniVa pair_of_cursors[2];

/* Parameters of typedef'd array va_list are adjusted to state pointers.
 * The callee must advance the caller's actual cursor object, not a local
 * pointer proxy. va_copy also accepts an adjusted parameter as its source. */
static int consume_one(MiniVa cursor) {
    return __builtin_va_arg(cursor, int);
}

static int copy_from_parameter(MiniVa cursor) {
    MiniVa independent;
    int value;
    __builtin_va_copy(independent, cursor);
    value = __builtin_va_arg(independent, int);
    __builtin_va_end(independent);
    return value;
}

static int exercise_parameter_decay(unsigned int fixed, ...) {
    MiniVa cursor;
    int observed;
    __builtin_va_start(cursor, fixed);
    observed = consume_one(cursor);
    if (observed != 11) return 3;
    if (copy_from_parameter(cursor) != 12) return 4;
    if (__builtin_va_arg(cursor, int) != 12) return 5;
    if (__builtin_va_arg(cursor, int) != 13) return 6;
    __builtin_va_end(cursor);
    return 0;
}

static int format_remaining(unsigned int fixed, ...) {
    MiniVa current;
    MiniVa copy;
    char buffer[32];
    int length;

    __builtin_va_start(current, fixed);
    if (__builtin_va_arg(current, int) != 41) return 1;
    __builtin_va_copy(copy, current);
    length = vsnprintf(buffer, sizeof(buffer), "%d/%ld", copy);
    __builtin_va_end(copy);
    __builtin_va_end(current);
    if (length != 5 || buffer[0] != '7' || buffer[1] != '/' ||
        buffer[2] != '9' || buffer[3] != '0' || buffer[4] != '0' ||
        buffer[5] != '\0') return 2;
    return 0;
}

int main(void) {
    MiniVa local;
    struct EmbeddedVa container;
    if (sizeof(MiniVa) != 24U || sizeof(local) != 24U ||
        sizeof(global_cursor) != 24U) return 10;
    if (sizeof(pair_of_cursors) != 48U || sizeof(container) != 40U ||
        sizeof(container.cursor) != 24U) return 11;
    if ((char *)(&local + 1) - (char *)&local != 24) return 12;
    if (format_remaining(9U, 41, 7, 900L) != 0) return 20;
    if (exercise_parameter_decay(4U, 11, 12, 13) != 0) return 21;
    return 0;
}
