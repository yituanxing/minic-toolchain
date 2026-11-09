#pragma GCC push_options
#pragma GCC target("+nothing+simd")
#pragma GCC pop_options
#pragma GCC aarch64 "arm_neon.h"
int invalid_neon_header_popped(void) { return 1; }
