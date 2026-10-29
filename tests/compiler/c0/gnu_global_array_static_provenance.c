/* A string literal and an ordinary static array are addressable global
 * objects. Their element-zero address is a static symbol, not a runtime
 * pointer fetched from an invented local snapshot. A dynamic index still
 * has to be evaluated for effects and must never inherit this optimization. */
static const char *name_from_literal(void) {
    return &"kernel/trace.c"[0];
}

static const char file_name[] = "driver/example.c";
static volatile unsigned char index_changes;

static const char *name_from_global(void) {
    return &file_name[0];
}

static char name_dynamic(unsigned limit) {
    unsigned index = index_changes++;
    return file_name[index % limit];
}

int main(void) {
    const char *literal = name_from_literal();
    const char *global = name_from_global();
    if (literal[0] != 'k' || literal[7] != 't' ||
        global[0] != 'd' || global[7] != 'e')
        return 1;
    if (name_dynamic(4) != 'd' || name_dynamic(4) != 'r' ||
        index_changes != 2)
        return 2;
    return 0;
}
