/* AArch64 AAPCS64's compiler builtin is a declaration specifier in block
 * scopes as well as in formal parameters and typedefs. Its internal proxy
 * remains owned by TargetInfo; name classification must not invent an ISA. */
#if defined(INVALID_CONST_DEST)
int bad_va_copy(__builtin_va_list source) {
    const __builtin_va_list independent = source;
    __builtin_va_copy(independent, source);
    return 0;
}
#elif defined(INVALID_INTEGER_DEST)
int bad_va_copy(__builtin_va_list source) {
    int independent = 0;
    __builtin_va_copy(independent, source);
    return 0;
}
#elif defined(INVALID_INTEGER_SOURCE)
int bad_va_copy(__builtin_va_list source) {
    __builtin_va_list independent;
    __builtin_va_copy(independent, 0);
    return 0;
}
#else
typedef __builtin_va_list MiniAArch64VaList;
int builtin_va_declaration_probe(__builtin_va_list source) {
    __builtin_va_list independent;
    MiniAArch64VaList typed_independent;
    __builtin_va_copy(independent, source);
    __builtin_va_copy(typed_independent, independent);
    /* `for` initializers consult the same declaration-vs-expression probe
     * as block statements. Ensure both accept the builtin type spelling. */
    for (__builtin_va_list loop_cursor = typed_independent; 0;) {
        __builtin_va_end(loop_cursor);
    }
    __builtin_va_end(typed_independent);
    __builtin_va_end(independent);
    return 0;
}
#endif
