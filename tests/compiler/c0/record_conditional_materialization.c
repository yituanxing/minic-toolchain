typedef struct {
    unsigned long bits;
} pgprot_t;

static pgprot_t pgprot_from_va(unsigned long value) {
    return (pgprot_t){value + 1UL};
}

static unsigned long consume(pgprot_t value) {
    return value.bits;
}

static unsigned long assign_then_read(int early, unsigned long value) {
    pgprot_t selected;

    selected = early ? (pgprot_t){7UL} : pgprot_from_va(value);
    return selected.bits;
}

int main(void) {
    if (consume(1 ? (pgprot_t){3UL} : pgprot_from_va(10UL)) != 3UL) {
        return 1;
    }
    if (consume(0 ? (pgprot_t){3UL} : pgprot_from_va(10UL)) != 11UL) {
        return 2;
    }
    if (assign_then_read(0, 20UL) != 21UL) {
        return 3;
    }
    return assign_then_read(1, 20UL) == 7UL ? 0 : 4;
}
