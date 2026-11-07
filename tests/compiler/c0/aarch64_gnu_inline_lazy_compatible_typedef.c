#pragma GCC push_options
#pragma GCC target ("+nothing+simd")
#pragma GCC aarch64 "arm_neon.h"
typedef unsigned long compatible_header_type;
typedef unsigned long compatible_header_type;
#pragma GCC pop_options
int main(void) { compatible_header_type value = 17UL; return (int)value; }
