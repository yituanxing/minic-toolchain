int rv32_overaligned_frame(int p0, int p1, int p2, int p3,
                           int p4, int p5, int p6, int p7,
                           int p8, int p9) {
    char huge[7] __attribute__((aligned(64)));
    huge[0] = (char)p9;
    return (int)huge[0] + (int)((unsigned long)huge & 63u) + p8;
}
