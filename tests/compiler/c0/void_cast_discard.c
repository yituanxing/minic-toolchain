static int counter;

static int bump(void) {
    counter += 1;
    return counter;
}

int main(void) {
    (void)0;
    (void)bump();
    return counter == 1 ? 0 : 1;
}
