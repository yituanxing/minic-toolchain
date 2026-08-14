extern __attribute__((__format__(printf, 4, 5)))
void warn_slowpath_fmt(const char *file,
                       const int line,
                       unsigned taint,
                       const char *fmt,
                       ...);

extern __attribute__((__format__(printf, 1, 2)))
void warn_printk(const char *fmt, ...);

int declaration_head_probe(int value) {
    warn_slowpath_fmt("probe.c", 7, 0U, "%d", value);
    warn_printk("%d", value);
    return value;
}
