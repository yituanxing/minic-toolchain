#pragma GCC push_options
#pragma GCC target ("+nothing+simd")
static int level_one(void) { return 11; }
#pragma GCC push_options
#pragma GCC target("+nothing+simd")
static int level_two(void) { return 20; }
#pragma GCC pop_options
static int restored_one(void) { return 7; }
#pragma GCC pop_options
int main(void) { return level_one() + level_two() - restored_one() - 24; }
