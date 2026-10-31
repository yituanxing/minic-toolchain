#pragma GCC push_options
#pragma GCC target("+nothing+simd")
#pragma GCC aarch64 "arm_neon.h"
/* Isolate the internal pragma emitted by GCC arm_neon.h. This is not a
 * full-header or NEON-intrinsic compilation test. */
int invalid_gnu_neon_header(void) { return 1; }
