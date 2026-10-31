volatile int runtime_bias = 11;
static const char runtime_text[] = "gnu-c-only";

int runtime_transform(int value) {
    return value * 7 + runtime_bias;
}

const char *runtime_message(void) {
    return runtime_text;
}
