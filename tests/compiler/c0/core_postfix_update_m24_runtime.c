extern unsigned int core_m24_postdec_value(unsigned int *);
extern unsigned int core_m24_countdown(unsigned int);
extern unsigned short *core_m24_pointer_increment(unsigned short *);
extern unsigned short *core_m24_pointer_decrement(unsigned short *);
extern void core_m24_swab16_shape(unsigned short *, unsigned int);

int main(void) {
    unsigned int value = 9;
    unsigned short data[4] = {1, 2, 3, 4};
    if (core_m24_postdec_value(&value) != 9 || value != 8)
        return 1;
    if (core_m24_countdown(7) != 7)
        return 2;
    if (core_m24_pointer_increment(&data[1]) != &data[2])
        return 3;
    if (core_m24_pointer_decrement(&data[2]) != &data[1])
        return 4;
    core_m24_swab16_shape(data, 4);
    if (data[0] != 1 || data[1] != 2 || data[2] != 3 || data[3] != 4)
        return 5;
    return 0;
}
