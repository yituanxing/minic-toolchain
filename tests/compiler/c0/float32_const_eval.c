static int direct_float = (int)1.5f;
static int added_float = (int)(1.5f + 2.0f);
static int negative_float = (int)(-2.5f);
int main(void) {
    return direct_float == 1 && added_float == 3 && negative_float == -2 ? 0 : 1;
}
