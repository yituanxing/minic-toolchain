static inline int add_one(int value) {
    return value + 1;
}

inline static int add_two(int value) {
    return value + 2;
}

int main(void) {
    return add_one(2) == 3 && add_two(2) == 4 ? 0 : 1;
}
