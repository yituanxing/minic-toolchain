void lsx_raid_step(unsigned char *p, unsigned char *q) {
    asm volatile("vld $vr0, %0" : : "m"(*p));
    asm volatile("vori.b $vr4, $vr0, 0");
    asm volatile("vslti.b $vr12, $vr4, 0");
    asm volatile("vslli.b $vr16, $vr4, 1");
    asm volatile("vandi.b $vr12, $vr12, 0x1d");
    asm volatile("vxor.v $vr16, $vr16, $vr12");
    asm volatile("vst $vr16, %0" : "=m"(*q));
}
void lasx_recover_step(unsigned char *p, unsigned char *q) {
    asm volatile("xvld $xr20, %0" : : "m"(*p));
    asm volatile("xvreplve0.q $xr22, $xr20");
    asm volatile("xvsrli.b $xr4, $xr22, 4");
    asm volatile("xvandi.b $xr4, $xr4, 0x0f");
    asm volatile("xvshuf.b $xr2, $xr22, $xr22, $xr4");
    asm volatile("xvxor.v $xr4, $xr4, $xr2");
    asm volatile("xvst $xr4, %0" : "=m"(*q));
}
void raid_eight_memory_rw(
    unsigned char *p0, unsigned char *p1,
    unsigned char *p2, unsigned char *p3,
    unsigned char *p4, unsigned char *p5,
    unsigned char *p6, unsigned char *p7) {
    asm volatile(
        "vld $vr0, %0\n\t"
        "vld $vr1, %1\n\t"
        "vld $vr2, %2\n\t"
        "vld $vr3, %3\n\t"
        "vld $vr4, %4\n\t"
        "vld $vr5, %5\n\t"
        "vld $vr6, %6\n\t"
        "vld $vr7, %7\n\t"
        "vxor.v $vr0, $vr0, $vr4\n\t"
        "vxor.v $vr1, $vr1, $vr5\n\t"
        "vst $vr0, %0\n\t"
        "vst $vr1, %1\n\t"
        "vst $vr2, %2\n\t"
        "vst $vr3, %3\n\t"
        "vst $vr4, %4\n\t"
        "vst $vr5, %5\n\t"
        "vst $vr6, %6\n\t"
        "vst $vr7, %7\n\t"
        : "+m"(*p0), "+m"(*p1), "+m"(*p2), "+m"(*p3),
          "+m"(*p4), "+m"(*p5), "+m"(*p6), "+m"(*p7));
}
void xor_named_pointer(unsigned long *v1, const unsigned long *v2) {
    asm volatile(
        "vld $vr0, %[v1], 0\n\t"
        "vld $vr1, %[v1], 16\n\t"
        "vld $vr4, %[v2], 0\n\t"
        "vld $vr5, %[v2], 16\n\t"
        "vxor.v $vr0, $vr0, $vr4\n\t"
        "vxor.v $vr1, $vr1, $vr5\n\t"
        "vst $vr0, %[v1], 0\n\t"
        "vst $vr1, %[v1], 16\n\t"
        : : [v1] "r"(v1), [v2] "r"(v2) : "memory");
}
