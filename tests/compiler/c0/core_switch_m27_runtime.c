int core_m27_switch_simple(int value);
int core_m27_switch_range(int value);
int core_m27_switch_fallthrough(int value);
int core_m27_printk_get_level(const char *buffer);

int main(void) {
    const char level3[] = {'\001', '3', 0};
    const char levelc[] = {'\001', 'c', 0};
    const char plain[] = {'x', '3', 0};

    if (core_m27_switch_simple(1) != 10 || core_m27_switch_simple(3) != 20 ||
        core_m27_switch_simple(9) != 30) {
        return 1;
    }
    if (core_m27_switch_range(4) != 1 || core_m27_switch_range(7) != 1 ||
        core_m27_switch_range(8) != 0) {
        return 2;
    }
    if (core_m27_switch_fallthrough(1) != 3 || core_m27_switch_fallthrough(2) != 2 ||
        core_m27_switch_fallthrough(9) != 9) {
        return 3;
    }
    if (core_m27_printk_get_level(level3) != '3' || core_m27_printk_get_level(levelc) != 'c' ||
        core_m27_printk_get_level(plain) != 0) {
        return 4;
    }
    return 0;
}
