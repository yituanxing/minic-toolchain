/* An incomplete `extern T objects[]` remains distinct from GNU `T[0]`.
 * Completing the zero-extent address-of case must not legalize sizeof here. */
extern int objects[];
int main(void) { return (int)sizeof(objects); }
