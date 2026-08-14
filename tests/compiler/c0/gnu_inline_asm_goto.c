static int dynamic_jump_label(int *key, int branch) {
    asm goto(
        "nop\n\t"
        ".long %l[label] - .\n\t"
        ".dword %0 - .\n\t"
        :
        : "i"(&((char *)key)[branch])
        :
        : label);

    return 0;
label:
    return 1;
}

static int literal_alternative(void) {
    asm goto(
        "j %l[yes]\n\t"
        ".word %[ext]\n\t"
        :
        : [ext] "i"(33)
        :
        : yes);

    return 0;
yes:
    return 1;
}

int main(void) {
    int key = 0;
    return dynamic_jump_label(&key, 0) + literal_alternative();
}
