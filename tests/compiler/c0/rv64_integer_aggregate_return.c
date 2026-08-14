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
