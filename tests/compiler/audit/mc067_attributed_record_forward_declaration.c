/* MC-067: GNU attributes may appear between struct and its forward tag.
 * -DMC067_PLAIN_CONTROL removes only the attribute; both forms are legal
 * GNU11, and the full definition must still refer to the same forward tag. */
#ifdef MC067_PLAIN_CONTROL
struct Forward;
#else
struct __attribute__((aligned(16))) Forward;
#endif
struct Forward { int x; };
int main(void) { return (int)sizeof(struct Forward); }
