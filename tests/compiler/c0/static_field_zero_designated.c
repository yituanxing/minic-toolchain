/* GNU accepts an empty designated zero-length array initializer.  Probe the
 * current MiniC source acceptance separately; it must not be called a GNU
 * negative merely because a MiniC parser policy may reject this extension. */
struct Z {
    int empty[0];
    int following;
};
static struct Z item = { .empty = {}, .following = 5 };
int main(void) { return item.following == 5 ? 42 : 1; }
