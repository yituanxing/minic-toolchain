/* Linux drm_edid-like const array lookup through an always-inline pointer
 * getter with a fixed argument and source-level compile-time guards. */
struct Mode {
    unsigned int clock;
    unsigned int vtotal;
};

static const struct Mode modes[4] = {
    {600, 262}, {600, 262}, {500, 312}, {500, 312}
};
static const struct Mode extended_modes[2] = {
    {700, 262}, {800, 312}
};

static __attribute__((always_inline)) inline const struct Mode *mode_for_vic(
    unsigned char vic)
{
    do {
        extern void minic_bad_table_size(void)
            __attribute__((error("table size not known")));
        if (sizeof(modes) / sizeof(modes[0]) != 4)
            minic_bad_table_size();
    } while (0);
    if (vic >= 1 && vic < 1 + sizeof(modes) / sizeof(modes[0]))
        return &modes[vic - 1];
    return (void *)0;
}

static volatile unsigned int side_effect_count;

/* Linux drm_edid's second BUILD_BUG_ON guards a second table, followed by a
 * second independently reachable conditional return. Both proven guards
 * are non-effectful; runtime-dependent or effectful prefixes are not. */
static __attribute__((always_inline)) inline const struct Mode *
mode_for_extended_vic(unsigned char vic)
{
    do {
        extern void minic_bad_first_table(void)
            __attribute__((error("first table size mismatch")));
        if (sizeof(modes) / sizeof(modes[0]) != 4)
            minic_bad_first_table();
    } while (0);
    do {
        extern void minic_bad_extended_table(void)
            __attribute__((error("extended table size mismatch")));
        if (sizeof(extended_modes) / sizeof(extended_modes[0]) != 2)
            minic_bad_extended_table();
    } while (0);
    if (vic >= 1 && vic < 5)
        return &modes[vic - 1];
    if (vic >= 193 && vic < 195)
        return &extended_modes[vic - 193];
    return (void *)0;
}

static __attribute__((always_inline)) inline const struct Mode *
effectful_mode_for_vic(unsigned char vic)
{
    ++side_effect_count;
    if (vic >= 1 && vic < 5)
        return &modes[vic - 1];
    return (void *)0;
}

static const struct Mode *impure_mode_for_vic(unsigned char vic)
{
    ++side_effect_count;
    if (vic >= 1 && vic < 1 + sizeof(modes) / sizeof(modes[0]))
        return &modes[vic - 1];
    return (void *)0;
}

extern void minic_bad_vic_field(void)
    __attribute__((error("VIC table field mismatch")));

int main(void)
{
    if (mode_for_vic(1)->vtotal != 262 ||
        mode_for_vic(2)->clock != 600 ||
        mode_for_vic(3)->vtotal != 312 ||
        mode_for_vic(4)->clock != 500 ||
        mode_for_extended_vic(1)->vtotal != 262 ||
        mode_for_extended_vic(4)->clock != 500)
        minic_bad_vic_field();
    if (impure_mode_for_vic(1)->vtotal != 262 || side_effect_count != 1)
        return 1;
    if (effectful_mode_for_vic(1)->vtotal != 262 || side_effect_count != 2)
        return 2;
    return 0;
}
