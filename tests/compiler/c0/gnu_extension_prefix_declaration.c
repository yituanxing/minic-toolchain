__extension__ typedef __signed__ long long kernel_s64;
__extension__ typedef unsigned long long kernel_u64;

kernel_s64 extension_sum(kernel_s64 left, kernel_u64 right) {
    return left + (kernel_s64)right;
}
