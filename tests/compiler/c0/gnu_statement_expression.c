static int statement_value(int input) {
    int value = input;

    return ({
        do {
            value += 2;
        } while (0);
        value;
    });
}

int main(void) {
    return statement_value(5) == 7 ? 0 : 1;
}
