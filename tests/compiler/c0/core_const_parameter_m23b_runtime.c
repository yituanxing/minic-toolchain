extern unsigned long core_m23b_const_parameter(unsigned long);
extern unsigned long core_m23b_const_pointer_parameter(int *);

int main(void) {
    int value = 37;
    if (core_m23b_const_parameter(41UL) != 42UL)
        return 1;
    if (core_m23b_const_pointer_parameter(&value) != 37UL)
        return 2;
    return 0;
}
