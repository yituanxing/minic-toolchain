struct pair64 {
    long first;
    long second;
};

static struct pair64 add_pair(struct pair64 lhs, struct pair64 rhs) {
    struct pair64 result;

    result.first = lhs.first + rhs.first;
    result.second = lhs.second + rhs.second;
    return result;
}

static struct pair64 forward_pair(struct pair64 lhs, struct pair64 rhs) {
    return add_pair(lhs, rhs);
}

int main(void) {
    return 0;
}

struct word32 {
    unsigned int value;
};

static unsigned int unwrap_word(struct word32 input) {
    return input.value;
}

static struct word32 return_word(struct word32 input) {
    return input;
}

static unsigned int call_unwrap_word(void) {
    struct word32 input;
    input.value = 7U;
    return unwrap_word(input);
}

struct nested_word {
    unsigned long value;
};

static struct nested_word nested_identity(struct nested_word value) {
    return value;
}

static struct nested_word nested_combine(struct nested_word left, struct nested_word right) {
    struct nested_word result;
    result.value = left.value | right.value;
    return result;
}

static struct nested_word nested_call_argument(struct nested_word left, struct nested_word right) {
    return nested_combine(left, nested_identity(right));
}
