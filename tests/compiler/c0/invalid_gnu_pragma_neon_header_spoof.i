#pragma GCC push_options
#pragma GCC target("+nothing+simd")
#pragma GCC aarch64 "arm_neon_extra.h"
int invalid_neon_header_spoof(void) { return 1; }
