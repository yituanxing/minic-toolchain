int static_fixed_table_value(int index) {
    static const unsigned char table[5] = {1, 2, 3};
    return table[index];
}
