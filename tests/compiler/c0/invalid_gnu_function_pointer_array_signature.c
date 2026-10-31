/* Passing a callback with a different nested array extent remains invalid.
 * Structural function type matching must preserve fixed array bounds. */
typedef unsigned long long u64;

typedef void (*callback_2)(const u64 h[][2]);

static void bad_callback(const u64 h[][3]) {
    (void)h;
}

static void accept_callback(callback_2 cb) {
    (void)cb;
}

void reject_different_nested_array_extent(void) {
    accept_callback(bad_callback);
}
