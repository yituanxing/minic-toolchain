#pragma GCC push_options
#pragma GCC target("+nothing+simd", "+aes")
int invalid_gnu_target_syntax(void) { return 1; }
