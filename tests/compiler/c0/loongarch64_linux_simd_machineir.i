void xor_lsx_selected(unsigned long *dst, const unsigned long *src) {
    asm volatile(
        "vld $vr0, %[v1], 0\n\t"
        "vld $vr1, %[v1], 16\n\t"
        "vld $vr4, %[v2], 0\n\t"
        "vld $vr5, %[v2], 16\n\t"
        "vxor.v $vr0, $vr0, $vr4\n\t"
        "vxor.v $vr1, $vr1, $vr5\n\t"
        "vst $vr0, %[v1], 0\n\t"
        "vst $vr1, %[v1], 16\n\t"
        : : [v1] "r"(dst), [v2] "r"(src) : "memory");
}
void xor_lasx_selected(unsigned long *dst, const unsigned long *src) {
    asm volatile(
        "xvld $xr0, %[v1], 0\n\t"
        "xvld $xr4, %[v2], 0\n\t"
        "xvxor.v $xr0, $xr0, $xr4\n\t"
        "xvst $xr0, %[v1], 0\n\t"
        : : [v1] "r"(dst), [v2] "r"(src) : "memory");
}
