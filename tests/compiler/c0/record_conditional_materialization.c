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

static unsigned long selected_member(int early, unsigned long first,
                                     unsigned long second) {
    /* Linux pgprot_t idiom: project a field from a conditional record rvalue.
     * Each arm owns a distinct compound literal initializer. */
    return (early ? (pgprot_t){first + 3UL}
                  : (pgprot_t){second + 5UL}).bits;
}

static unsigned long selected_call_count;

static pgprot_t selected_true(void) {
    ++selected_call_count;
    return (pgprot_t){17UL};
}

static pgprot_t selected_false(void) {
    selected_call_count += 100UL;
    return (pgprot_t){23UL};
}

static unsigned long selected_call_member(int early) {
    return (early ? selected_true() : selected_false()).bits;
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
    if (assign_then_read(1, 20UL) != 7UL) {
        return 4;
    }
    if (selected_member(1, 31UL, 40UL) != 34UL ||
        selected_member(0, 31UL, 40UL) != 45UL) {
        return 5;
    }
    if (selected_call_member(1) != 17UL || selected_call_count != 1UL) {
        return 6;
    }
    if (selected_call_member(0) != 23UL || selected_call_count != 101UL) {
        return 7;
    }
    return 0;
}
