unsigned long external_too_wide = (1UL << 40);

int main(void) {
    return external_too_wide != 0;
}
