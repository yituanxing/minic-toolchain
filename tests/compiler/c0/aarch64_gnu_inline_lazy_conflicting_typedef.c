#pragma GCC push_options
#pragma GCC target ("+nothing+simd")
#pragma GCC aarch64 "arm_neon.h"
typedef unsigned int conflicting_header_type;
typedef unsigned long conflicting_header_type;
#pragma GCC pop_options
int main(void) { return 17; }
