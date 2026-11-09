/* GNU C oracle: definitions in nested scopes shadow outer enum tags.
 * Leaving a block restores the visible enclosing tag and its enumerators. */
enum scoped_mode { MODE_GLOBAL = 11 };

static int inner_definition(void) {
    enum scoped_mode { MODE_FUNCTION = 22 };
    enum scoped_mode value = MODE_FUNCTION;
    return (int)value;
}

static int nested_definitions(void) {
    enum scoped_mode { MODE_LOCAL = 33 };
    int total = MODE_LOCAL;
    {
        enum scoped_mode { MODE_NESTED = 44 };
        enum scoped_mode nested = MODE_NESTED;
        total += (int)nested;
    }
    {
        enum scoped_mode current = MODE_LOCAL;
        total += (int)current;
    }
    return total;
}

static int restore_outer(void) {
    {
        enum scoped_mode { MODE_BLOCK = 55 };
        enum scoped_mode value = MODE_BLOCK;
        if ((int)value != 55) return -1;
    }
    enum scoped_mode outer = MODE_GLOBAL;
    return (int)outer;
}

static int forward_shadow(void) {
    /* A standalone forward tag introduces an incomplete declaration in
     * this block and must not inherit the completed file-scope entity. */
    enum scoped_mode;
    enum scoped_mode { MODE_FORWARD = 66 };
    enum scoped_mode value = MODE_FORWARD;
    return (int)value;
}

int main(void) {
    enum scoped_mode outer = MODE_GLOBAL;
    return inner_definition() == 22 && nested_definitions() == 110 &&
           restore_outer() == 11 && forward_shadow() == 66 &&
           outer == MODE_GLOBAL ? 0 : 1;
}
