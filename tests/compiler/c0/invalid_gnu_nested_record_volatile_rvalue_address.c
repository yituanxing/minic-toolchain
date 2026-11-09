/* A volatile field of a materialized record rvalue is still an rvalue:
 * recording its address would expose a temporary with invalid lifetime. */
typedef struct Inner { volatile unsigned int mark; } Inner;
typedef struct Outer { Inner inner; } Outer;
static Outer make_outer(void) {
    Outer result = { { 7U } };
    return result;
}
volatile unsigned int *invalid_nested_volatile_member_address(void) {
    return &({ Outer result = make_outer(); result; }).inner.mark;
}
