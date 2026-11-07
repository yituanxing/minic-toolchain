/* A field selected from a statement-expression record rvalue is NOT a C
 * object lvalue.  Removing top-level cv from its value must not allow &. */
typedef struct KUid { unsigned int val; } KUid;
KUid *invalid(void) {
    return &({ KUid temporary = { 1U }; temporary; });
}
