static int value;
static int *selected = 1 ? &value : 0;
static int *null_selected = 0 ? &value : 0;
int main(void) {
    return selected == &value && null_selected == 0 ? 0 : 1;
}
