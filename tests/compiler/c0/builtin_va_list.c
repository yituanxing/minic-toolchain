typedef __builtin_va_list MiniBuiltinVaList;

void *builtin_va_list_as_pointer(MiniBuiltinVaList value) {
    return value;
}

MiniBuiltinVaList builtin_va_list_roundtrip(void *value) {
    MiniBuiltinVaList result = value;
    return result;
}
