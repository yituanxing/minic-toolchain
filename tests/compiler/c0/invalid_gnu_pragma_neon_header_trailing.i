#pragma GCC push_options
#pragma GCC target("+nothing+simd")
#pragma GCC aarch64 "arm_neon.h" extra
int invalid_neon_header_trailing(void) { return 1; }
