/* Linux uses absolute nonzero pointer sentinels in file-scope objects.
 * The constant is not an ELF symbol relocation and has to occupy the
 * target-width pointer slot with the GNU implementation-defined bits. */
static void *empty_list = (void *)-1L;
static unsigned char *guard_page = (unsigned char *)0x1000L;

void *read_empty_list(void) { return empty_list; }
unsigned char *read_guard_page(void) { return guard_page; }

/* A pointer converted to C _Bool is 1 for any nonnull pointer, never the
 * low byte of its address.  Both LP64 direct machine targets must normalize
 * the full pointer register before writing the Core scalar value. */
_Bool pointer_is_nonnull(const void *pointer) { return (_Bool)pointer; }
