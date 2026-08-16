extern int core_basic_math(int x, int y);
extern int core_branch(int x);
extern int core_ninth(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8);

int main(void) {
    if (core_basic_math(2, 3) != 0) {
        return 1;
    }
    if (core_basic_math(2, -2) != 1) {
        return 2;
    }
    if (core_branch(0) != -3) {
        return 3;
    }
    if (core_branch(4) != 7) {
        return 4;
    }
    if (core_ninth(1, 2, 3, 4, 5, 6, 7, 8, 9) != 9) {
        return 5;
    }
    return 0;
}
