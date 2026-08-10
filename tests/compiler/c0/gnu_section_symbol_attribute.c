extern char __attribute__((__section__(".probe.data"))) placed_data[];
char placed_data[] = "x";

extern unsigned long suffix_first __attribute__((__section__(".probe.suffix.first"))),
    suffix_second __attribute__((__section__(".probe.suffix.second")));
unsigned long suffix_first = 7;
unsigned long suffix_second = 9;

extern char suffix_array[] __attribute__((__section__(".probe.suffix.array")));
char suffix_array[] = "z";

void __attribute__((__section__(".probe.text"))) placed_function(void);

void placed_function(void) {
}

int main(void) {
    placed_function();
    return placed_data[0] == 'x' && suffix_first == 7 && suffix_second == 9 &&
                   suffix_array[0] == 'z'
               ? 0
               : 1;
}
