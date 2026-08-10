extern char __attribute__((__section__(".probe.data"))) placed_data[];
char placed_data[] = "x";

void __attribute__((__section__(".probe.text"))) placed_function(void);

void placed_function(void) {
}

int main(void) {
    placed_function();
    return placed_data[0] == 'x' ? 0 : 1;
}
