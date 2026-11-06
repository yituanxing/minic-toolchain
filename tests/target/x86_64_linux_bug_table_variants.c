/* Original Linux 6.6 BUG/WARN local-constant and metadata encodings. These
 * contain UD2 and must only be compiled/assembled; never execute them. */
struct bug_entry {
    int bug_addr;
    int file;
    unsigned short line;
    unsigned short flags;
};

void linux_bug_numeric_no_discard(void) {
    __auto_type __flags = 0;
    asm volatile("1:\t" ".byte 0x0f, 0x0b" "\n"
        ".pushsection __bug_table,\"aw\"\n"
        "2:\t" ".long " "1b" " - ." "\t# bug_entry::bug_addr\n"
        "\t" ".long " "%c0" " - ." "\t# bug_entry::file\n"
        "\t.word %c1" "\t# bug_entry::line\n"
        "\t.word %c2" "\t# bug_entry::flags\n"
        "\t.org 2b+%c3\n"
        ".popsection\n" ""
        : : "i"("fs/ext4/fsync.c"), "i"(150), "i"(__flags),
            "i"(sizeof(struct bug_entry)));
}

void linux_bug_numeric_reachable(void) {
    asm volatile("1:\t.byte 0x0f, 0x0b\n"
        ".pushsection __bug_table,\"aw\"\n"
        "2:\t.long 1b - .\n"
        ".long %c0 - .\n"
        ".word %c1\n"
        ".word %c2\n"
        ".org 2b+%c3\n"
        ".popsection\n"
        "998:\n"
        ".pushsection .discard.reachable\n"
        ".long 998b\n"
        ".popsection\n"
        : : "i"("drivers/gpu/drm/drm_blend.c"), "i"(89), "i"(0),
            "i"(sizeof(struct bug_entry)));
}

void linux_bug_named_no_discard(void) {
    asm volatile("1:\t.byte 0x0f, 0x0b\n"
        ".pushsection __bug_table,\"aw\"\n"
        "2:\t.long 1b - .\n"
        ".long %c[path] - .\n"
        ".word %c[ln]\n"
        ".word %c[fl]\n"
        ".org 2b+%c[sz]\n"
        ".popsection\n"
        : : [path] "i"("include/linux/bug.h"),
            [ln] "i"(111), [fl] "i"(0),
            [sz] "i"(sizeof(struct bug_entry)));
}
