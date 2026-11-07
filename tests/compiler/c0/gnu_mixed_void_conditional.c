/* GNU mixed-void ternary is used by Linux x86's pgd_clear() macro. The
 * value-producing arm must still execute side effects exactly once. */
static int progress;

static void void_effect(int amount) {
    progress += amount;
}

static int value_effect(int amount) {
    progress += amount;
    return amount * 2;
}

void minic_mixed_void_conditional(int cond) {
    cond ? void_effect(3) : value_effect(5);
    cond ? value_effect(7) : void_effect(11);
}

void minic_constant_mixed_void(void) {
    1 ? void_effect(13) : value_effect(17);
    0 ? value_effect(19) : void_effect(23);
}

int main(void) {
    minic_mixed_void_conditional(1);
    minic_mixed_void_conditional(0);
    minic_constant_mixed_void();
    return progress == (3 + 7 + 5 + 11 + 13 + 23) ? 0 : 1;
}
