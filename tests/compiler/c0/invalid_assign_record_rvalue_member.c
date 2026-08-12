typedef struct {
    unsigned long value;
} sample_t;

sample_t make_sample(sample_t value)
{
    return value;
}

int main(void)
{
    sample_t value = { 1 };
    make_sample(value).value = 7;
    return 0;
}
