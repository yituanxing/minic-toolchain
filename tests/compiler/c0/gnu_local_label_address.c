static unsigned long first_local_label(void) {
    return ({
        __label__ here;
    here:
        (unsigned long)&&here;
    });
}

static unsigned long repeated_local_labels(int pick_first) {
    unsigned long first = ({
        __label__ here;
    here:
        (unsigned long)&&here;
    });
    unsigned long second = ({
        __label__ here;
    here:
        (unsigned long)&&here;
    });

    return pick_first ? first : second;
}

static unsigned long forward_local_label(void) {
    return ({
        __label__ later;
        unsigned long address = (unsigned long)&&later;
    later:
        address;
    });
}

int main(void) {
    unsigned long a = first_local_label();
    unsigned long b = repeated_local_labels(1);
    unsigned long c = repeated_local_labels(0);
    unsigned long d = forward_local_label();

    return (a != 0UL && b != 0UL && c != 0UL && d != 0UL) ? 0 : 1;
}
