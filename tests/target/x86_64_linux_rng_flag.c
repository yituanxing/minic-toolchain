/* Linux rdrand_long/rdseed_long: the CF flag output and data result are
 * independent GNU outputs. Do not run without checking CPU capabilities. */
int linux_rdrand_long(unsigned long *result) {
    _Bool succeeded;
    asm volatile("rdrand %[out]"
        "\n\t/* output condition code " "c" "*/\n"
        : "=@cc" "c"(succeeded), [out] "=r"(*result));
    return succeeded;
}

int linux_rdseed_long(unsigned long *result) {
    _Bool succeeded;
    asm volatile("rdseed %[out]"
        "\n\t/* output condition code " "c" "*/\n"
        : "=@cc" "c"(succeeded), [out] "=r"(*result));
    return succeeded;
}
