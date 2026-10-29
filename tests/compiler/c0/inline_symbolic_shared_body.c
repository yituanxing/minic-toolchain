/* Every inlining specialization must preserve independent source bindings. */
static int fa(void) { return 11; }
static int fb(void) { return 22; }
__attribute__((always_inline)) static inline int leaf(int (*fn)(void), int k) {
    (void)k;
    return fn();
}
__attribute__((always_inline)) static inline int mid(int (*fn)(void), int k) {
    return leaf(fn, k);
}
int aa(void) { return mid(fa, 1); }
int bb(void) { return mid(fb, 1); }
static int values[] = { 31, 42 };
__attribute__((always_inline)) static inline int read_ptr(const int *p) {
    return *p;
}
__attribute__((always_inline)) static inline int read_index(int index) {
    return read_ptr(&values[index]);
}
int cc(void) { return read_index(0); }
int dd(void) { return read_index(1); }
int main(void) {
    return aa() == 11 && bb() == 22 && cc() == 31 && dd() == 42 ? 0 : 1;
}
