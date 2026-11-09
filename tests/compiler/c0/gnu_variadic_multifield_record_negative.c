typedef __builtin_va_list MiniVaList;
typedef struct { unsigned int first, second; } MiniPair;

unsigned int mini_va_unsupported_pair(unsigned int tag, ...) {
    MiniVaList ap;
    __builtin_va_start(ap, tag);
    MiniPair pair = __builtin_va_arg(ap, MiniPair);
    __builtin_va_end(ap);
    return pair.first + pair.second;
}
