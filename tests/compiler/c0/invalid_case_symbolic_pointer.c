static int object;

int probe(unsigned long value) {
    switch (value) {
    case (unsigned long)&object:
        return 1;
    default:
        return 0;
    }
}
