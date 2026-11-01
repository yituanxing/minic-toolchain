/* MC-067 rejection: attributes must not hide an incompatible tag kind. */
struct __attribute__((aligned(16))) KindMismatch;
union KindMismatch { int x; };
int main(void) { return 0; }
