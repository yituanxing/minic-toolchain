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

/* Linux inode_get_ctime(): two signed INTEGER eightbytes returned in
 * RAX/RDX and passed in the SysV GPR pair, not a hidden sret pointer. */
struct linux_timespec64 {
    long long tv_sec;
    long tv_nsec;
};

struct linux_timespec64 minic_linux_timespec_return(
    long long seconds, long nanos) {
    struct linux_timespec64 result;
    result.tv_sec = seconds;
    result.tv_nsec = nanos;
    return result;
}

long long minic_linux_timespec_sum(struct linux_timespec64 value) {
    return value.tv_sec + value.tv_nsec;
}

long long minic_linux_timespec_roundtrip(long long seconds, long nanos) {
    struct linux_timespec64 ts = minic_linux_timespec_return(seconds, nanos);
    return minic_linux_timespec_sum(ts);
}
