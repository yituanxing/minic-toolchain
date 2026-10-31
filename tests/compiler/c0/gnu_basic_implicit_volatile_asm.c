int basic_asm_marker(int value) {
    asm("nop");
    return value + 1;
}

int main(void) { return basic_asm_marker(6) == 7 ? 0 : 1; }
