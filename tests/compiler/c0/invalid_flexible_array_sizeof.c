struct Holder { int head; unsigned long values[]; };
unsigned long bad(struct Holder *holder) { return sizeof(holder->values); }
