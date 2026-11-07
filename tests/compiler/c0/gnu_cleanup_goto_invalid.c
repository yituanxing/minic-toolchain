static void cleanup_touch(int *value) { (void)value; }

void entering_cleanup_scope_is_invalid(void) {
    goto inner_label;
    {
        int guard __attribute__((cleanup(cleanup_touch))) = 1;
inner_label:
        guard += 1;
    }
}
