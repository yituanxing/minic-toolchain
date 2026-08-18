struct mapping {
    int value;
};
static struct mapping maps[] __attribute__((section(".data.test"))) = {
    {.value = 1},
    {.value = 2},
};
int main(void) {
    return maps[0].value + maps[1].value - 3;
}
