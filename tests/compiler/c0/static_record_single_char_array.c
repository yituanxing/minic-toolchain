struct header { unsigned int a, b, c; };
static const struct {
    struct header h;
    unsigned char name[sizeof("Linux")];
    typeof("") desc;
} note = {{sizeof("Linux"), sizeof(""), 0x100}, "Linux", ""};
int main(void) { return note.name[0] != 'L' || note.desc[0] != '\0'; }
