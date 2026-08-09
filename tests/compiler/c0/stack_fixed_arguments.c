int gcc_abi_sum9(int a, int b, int c, int d, int e, int f, int g, int h, int i);

int minic_abi_sum9(int a, int b, int c, int d, int e, int f, int g, int h, int i) {
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i;
}

int minic_call_gcc9(void) {
    return gcc_abi_sum9(1, 2, 3, 4, 5, 6, 7, 8, 9);
}
