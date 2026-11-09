/* Common declaration-only GNU attribute policy, independent of machine ABI. */
typedef int (__attribute__((unused)) *portable_callback_t)(int);

struct portable_callbacks {
    int (__attribute__((unused)) *field_callback)(int);
};

static int invoke_parameter(
    int (__attribute__((unused)) *parameter_callback)(int), int value) {
    return parameter_callback(value);
}

static int increment(int value) { return value + 1; }

int main(void) {
    portable_callback_t renamed = increment;
    struct portable_callbacks callbacks = { increment };
    return invoke_parameter(callbacks.field_callback, 40) == 41 &&
                   renamed(41) == 42
               ? 0 : 1;
}
