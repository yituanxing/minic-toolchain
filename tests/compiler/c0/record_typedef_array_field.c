typedef unsigned long GpState[4];

struct Context {
    GpState regs;
    unsigned marker;
};

unsigned long read_reg(struct Context *context, int index) {
    return context->regs[index];
}

unsigned long context_size(void) {
    return sizeof(struct Context);
}
