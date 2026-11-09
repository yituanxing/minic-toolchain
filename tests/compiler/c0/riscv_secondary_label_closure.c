/* A constant-specialized clone can retain an indirect-entry label whose
 * ordinary entry path was folded away.  This is a genuine Core entry root,
 * not permission for the backend to forge a branch from its ABI prologue. */
static inline int secondary_label(unsigned selector) {
    if (selector == 0U) return 13;
    if (selector == 1U) goto alternate;
    return 27;
alternate:
    return 41;
}

int secondary_first(void) { return secondary_label(0U); }
int secondary_second(void) { return secondary_label(1U); }
int secondary_third(unsigned selector) {
    return secondary_label(selector);
}
