/* Linux ptrace struct membuf: two INTEGER SysV eightbytes (pointer+size_t).
 * Both an incoming aggregate and an outgoing aggregate call are exercised. */
struct linux_membuf {
    void *p;
    unsigned long left;
};

unsigned long minic_linux_membuf_sum(struct linux_membuf buffer) {
    return (unsigned long)buffer.p + buffer.left;
}

unsigned long minic_linux_membuf_call(void *pointer, unsigned long length) {
    struct linux_membuf buffer;
    buffer.p = pointer;
    buffer.left = length;
    return minic_linux_membuf_sum(buffer);
}
