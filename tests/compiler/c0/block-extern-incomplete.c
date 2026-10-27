/* Original Linux 6.6.143 seq_user_ns shape: forward-declared record.
 * 'extern' is a no-storage entity declaration, legal until definition;
 * address-taking requires only a pointer to the incomplete record. */
struct user_namespace;
struct user_namespace *seq_user_ns(void) {
    extern struct user_namespace init_user_ns;
    return &init_user_ns;
}
