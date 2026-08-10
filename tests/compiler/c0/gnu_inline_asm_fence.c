static void write_fence(void) {
    __asm__ __volatile__("fence " "rw" "," "w" : : : "memory");
}

static void read_fence(void) {
    __asm__ __volatile__("fence " "r" "," "rw" : : : "memory");
}

int main(void) {
    write_fence();
    read_fence();
    return 0;
}
