/* Original GNU Linux x86-64 Kbuild mitigation shapes: direct, indirect and
 * label-address control flow. Link/run through external thunk definitions. */
static int minic_x86_add_five(int x) { return x + 5; }

int minic_x86_indirect_call(int (*fn)(int), int x) {
    return fn(x);
}

int minic_x86_computed_goto(int x) {
    static void *const labels[2] = { &&left, &&right };
    goto *labels[x & 1];
left:
    return x + 10;
right:
    return x + 20;
}

int main(void) {
    return minic_x86_indirect_call(minic_x86_add_five, 7) != 12 ||
           minic_x86_computed_goto(0) != 10 ||
           minic_x86_computed_goto(1) != 21;
}
