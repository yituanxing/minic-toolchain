int linux_printk_level_like(const char *buffer) {
    if (buffer[0] == '\001' && buffer[1]) {
        switch (buffer[1]) {
        case '0' ... '7':
            return buffer[1];
        case '\077':
            return 99;
        default:
            return 0;
        }
    }
    return -1;
}

int octal_character_values(void) {
    return '\0' + '\7' + '\12' + '\123';
}
