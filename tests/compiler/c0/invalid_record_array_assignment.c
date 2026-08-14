struct Holder { int values[4]; };
void bad(struct Holder *holder) { holder->values = 0; }
