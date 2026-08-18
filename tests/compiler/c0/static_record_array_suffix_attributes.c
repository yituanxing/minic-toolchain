struct mapping {
    int value;
};
static struct mapping maps[] __attribute__((section(".data.test"), aligned(16))) = {
    {.value = 1},
    {.value = 2},
};
int main(void) {
    return maps[1].value - 2;
}
