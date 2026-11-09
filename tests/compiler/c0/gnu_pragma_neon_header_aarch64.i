#pragma GCC push_options
#pragma GCC target ("+nothing+simd")
#pragma GCC aarch64 "arm_neon.h"
typedef unsigned char neon_header_marker_byte;
#pragma GCC pop_options
int main(void) { neon_header_marker_byte x = 0; return (int)x; }
