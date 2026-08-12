extern char __init_begin[4];
extern char __init_end[4];

unsigned long span(void) {
    extern char __init_begin[], __init_end[];
    return (unsigned long)&__init_end - (unsigned long)&__init_begin;
}

int main(void) {
    return span() == 4 ? 0 : 1;
}
