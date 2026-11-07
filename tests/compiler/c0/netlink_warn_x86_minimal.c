static volatile int bad_count;

__attribute__((noinline)) static int check(int flags) {
    int cnt = 0;
    if (flags & 2)
        cnt++;
    if (flags & 4)
        cnt++;
    if (!cnt)
        bad_count++;
    return cnt;
}

int main(void) {
    if (check(2) != 1) return 1;
    if (check(4) != 1) return 2;
    if (check(6) != 2) return 3;
    return bad_count == 0 ? 0 : 99;
}
