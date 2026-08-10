extern const unsigned long
cpu_bit_bitmap[64 + 1][(((64) + ((sizeof(long) * 8)) - 1) / ((sizeof(long) * 8)))];

unsigned long read_cpu_bit(unsigned int cpu, unsigned int word) {
    return cpu_bit_bitmap[cpu][word];
}

const unsigned long *cpu_bit_row(unsigned int cpu) {
    const unsigned long *row = cpu_bit_bitmap[1 + cpu % 64];
    return row;
}
