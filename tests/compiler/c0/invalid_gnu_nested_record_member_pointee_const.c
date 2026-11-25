typedef struct Inner { const int *const pointer; } Inner;
typedef struct Outer { Inner inner; } Outer;
static const int value = 7;
static Outer make_outer(void) {
    Outer result = { { &value } };
    return result;
}
void illegal_write_through_nested_record_rvalue(void) {
    *({ Outer result = make_outer(); result; }).inner.pointer = 42;
}
