typedef unsigned long long u64;
static const u64 minic_readonly[2][2] = {{1U, 2U}, {3U, 4U}};

static void minic_mutate(u64 values[][2]) {
    values[0][0] = 7U;
}

void minic_illegal_const_drop(void) {
    minic_mutate(minic_readonly);
}
