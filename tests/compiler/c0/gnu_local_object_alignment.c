struct alignment_probe {
    unsigned long first;
    unsigned long second;
};

int local_explicit_alignment(int value)
{
    char scratch[17] __attribute__((__aligned__(32)));
    scratch[0] = (char)value;
    return scratch[0];
}

int local_alignof_alignment(int value)
{
    char scratch[sizeof(struct alignment_probe) + 3]
        __attribute__((__aligned__(__alignof__(struct alignment_probe))));
    scratch[0] = (char)value;
    return scratch[0];
}
