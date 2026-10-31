/* Two otherwise identical callbacks must preserve their pointee element
 * qualifications.  A mutable u64 (*)[2] parameter cannot silently stand in
 * for a const u64 (*)[2] formal function type. */
typedef unsigned long long u64;

typedef void (*callback_const)(const u64 h[][2]);

static void callback_mutable(u64 h[][2]) {
    h[0][0] += 1U;
}

static void accept_callback(callback_const cb) {
    (void)cb;
}

void reject_different_nested_array_qualifiers(void) {
    accept_callback(callback_mutable);
}
