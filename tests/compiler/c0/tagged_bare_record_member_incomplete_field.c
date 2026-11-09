/* MC-063 negative: a named field still requires a complete object type;
 * only a bare tagged declaration is permitted to leave the tag incomplete. */
struct Missing;
struct Reject { struct Missing member; };
int main(void) { return 0; }
