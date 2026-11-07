/* GNU C folds pointer comparisons of two pure integer-address constants
 * in static aggregate integer initializers (Linux trace-event signedness). */
struct field {
    int is_signed;
    int null_conditional;
};

static struct field trace_fields[] = {
    { .is_signed = (((void *)(-1)) < (void *)1),
      .null_conditional = ((void *)0) ? sizeof(void *) - 1 : 0 },
    { .is_signed = (((int *)(-1)) > (int *)1),
      .null_conditional = ((void *)(-1)) ? 7 : 0 },
    { .is_signed = (((char *)(-1)) == (char *)1),
      .null_conditional = ((void *)0) ? 3 : 2 },
};

int main(void) {
    return !(trace_fields[0].is_signed == 0 &&
             trace_fields[0].null_conditional == 0 &&
             trace_fields[1].is_signed == 1 &&
             trace_fields[1].null_conditional == 7 &&
             trace_fields[2].is_signed == 0 &&
             trace_fields[2].null_conditional == 2);
}
