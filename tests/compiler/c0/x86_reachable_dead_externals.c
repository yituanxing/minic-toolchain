extern void __compiletime_assert_212(void);
extern void mesh_update(int);

static inline int feature_disabled(void) { return 0; }

int probe_disabled_mesh(int value) {
    if (feature_disabled())
        mesh_update(value);
    return value + 1;
}

int probe_dead_build_assert(int value) {
    if (feature_disabled() && value > 0)
        __compiletime_assert_212();
    return value + 2;
}

int probe_const_builtin(int value) {
    if (__builtin_constant_p(value) && value > 12345)
        __compiletime_assert_212();
    return value + 3;
}
