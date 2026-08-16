long reject_local_fixed(long value) {
    register long bad asm("s1") = value;
    return bad;
}
