typedef struct {
    unsigned long pgprot;
} pgprot_t;

pgprot_t pgprot_noncached(pgprot_t oldprot)
{
    return oldprot;
}

unsigned long project(pgprot_t oldprot)
{
    return (pgprot_noncached(oldprot)).pgprot;
}

int main(void)
{
    pgprot_t value = { 17 };
    return project(value) == 17 ? 0 : 1;
}
