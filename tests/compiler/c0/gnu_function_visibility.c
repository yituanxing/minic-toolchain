__attribute__((visibility("internal"))) extern int visible_api(int value);

__attribute__((visibility("internal"))) int visible_api(int value) {
    return value + 1;
}

int call_visible_api(void) {
    return visible_api(4);
}
