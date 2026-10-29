/* Plain byte strings cannot initialize a 16-bit EFI CHAR16 local array. */
static unsigned int minic_wide_invalid(void) {
    unsigned short buffer[] = "not-wide";
    return buffer[0];
}
int minic_wide_invalid_entry(void) { return (int)minic_wide_invalid(); }
