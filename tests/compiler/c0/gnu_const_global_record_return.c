/* Linux drm_edid-like const array lookup through an always-inline pointer
 * getter with a fixed argument and source-level compile-time guards. */
struct Mode {
    unsigned int clock;
    unsigned int vtotal;
};

static const struct Mode modes[4] = {
    {600, 262}, {600, 262}, {500, 312}, {500, 312}
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
        mode_for_vic(4)->clock != 500)
        minic_bad_vic_field();
    if (impure_mode_for_vic(1)->vtotal != 262 || side_effect_count != 1)
        return 1;
    return 0;
}
