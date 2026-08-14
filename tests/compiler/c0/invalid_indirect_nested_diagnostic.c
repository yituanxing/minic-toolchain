typedef int unary_fn_t(int value);

int bad_indirect_argument(unary_fn_t *fn) {
    return fn(missing_name);
}
