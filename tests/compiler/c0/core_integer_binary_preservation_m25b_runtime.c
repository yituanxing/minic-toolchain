unsigned short core_m25b_add(unsigned short *left, unsigned short *right);
unsigned short core_m25b_and(unsigned short *left, unsigned short *right);
unsigned short core_m25b_or(unsigned short *left, unsigned short *right);
void core_m25b_be16_shape(unsigned short *value, unsigned short addend);

int main(void) {
    unsigned short left = 0x0012U;
    unsigned short right = 0x0034U;
    unsigned short value = 0x1234U;

    if (core_m25b_add(&left, &right) != 0x0048U) {
        return 1;
    }
    if (core_m25b_and(&left, &right) != 0x0011U) {
        return 2;
    }
    if (core_m25b_or(&left, &right) != 0x0037U) {
        return 3;
    }
    core_m25b_be16_shape(&value, 1U);
    if (value != 0x1237U) {
        return 4;
    }
    return 0;
}
