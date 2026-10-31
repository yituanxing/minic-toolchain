/* A GNU record statement-expression is an rvalue; its pointer member's
 * outer const must vanish from Core SSA, while pointee const must survive. */
typedef struct QualLeaf {
    const int *const pointer;
    volatile unsigned int mark;
} QualLeaf;
typedef struct QualOuter { QualLeaf leaf; } QualOuter;

static const int selected_values[2] = { 19, 31 };
static unsigned int call_count;

__attribute__((noinline)) static QualOuter make_outer(int which) {
    QualOuter value = {
        { which ? &selected_values[1] : &selected_values[0],
          (unsigned int)(70 + which) }
    };
    call_count++;
    return value;
}

__attribute__((noinline)) const int *select_nested_pointer(int which) {
    return ({ QualOuter value = make_outer(which); value; }).leaf.pointer;
}

__attribute__((noinline)) unsigned int read_nested_volatile_twice(void) {
    QualOuter source = make_outer(0);
    /* Two distinct volatile scalar projections of a record rvalue. The
     * record copies must not suppress the member loads as ordinary SSA. */
    unsigned int first = ({ QualOuter value = source; value; }).leaf.mark;
    unsigned int second = ({ QualOuter value = source; value; }).leaf.mark;
    return first + second;
}

int main(void) {
    const int *left = select_nested_pointer(0);
    const int *right = select_nested_pointer(1);
    const unsigned int twice = read_nested_volatile_twice();
    return *left == 19 && *right == 31 && twice == 140 &&
           call_count == 3 ? 0 : 1;
}
