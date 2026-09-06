struct OffsetProbe {
    unsigned char lead;
    unsigned long value;
};

static const unsigned long value_offset =
    (unsigned long)&((struct OffsetProbe *)0)->value;

int main(void)
{
    return value_offset == 8U ? 0 : 1;
}
