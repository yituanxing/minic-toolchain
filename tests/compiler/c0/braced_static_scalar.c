static unsigned long value = {7};
static int *pointer = {
    0,
};
int main(void) {
    return value == 7 && pointer == 0 ? 0 : 1;
}
