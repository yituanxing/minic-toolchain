typedef __attribute__((__format__(printf, 2, 3))) int (*printer_t)(void *, const char *, ...);
typedef int __attribute__((nonnull(2, 3))) (*compare_t)(void *, const void *, const void *);

static int pick_first(int fixed, ...) {
    return fixed;
}

static int use_prefix_parameter(__attribute__((__unused__)) int ignored) {
    return pick_first(7, 11, 13);
}

static int use_post_type_parameter(int __attribute__((__unused__)) ignored) {
    return pick_first(5, 17);
}

int main(void) {
    printer_t printer = (printer_t)0;
    compare_t compare = (compare_t)0;

    if (printer != (printer_t)0 || compare != (compare_t)0) {
        return 1;
    }
    return use_prefix_parameter(0) + use_post_type_parameter(0) - 12;
}
