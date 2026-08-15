int __attribute__((__noinline__)) __attribute__((__no_instrument_function__))
__attribute__((__section__(".noinstr.text"))) __attribute__((__no_sanitize_address__))
__attribute__((__no_profile_instrument_function__))
no_profile_decl(int value);

__attribute__((__noinline__)) __attribute__((__no_instrument_function__))
__attribute__((__section__(".noinstr.text"))) __attribute__((__no_sanitize_address__))
__attribute__((__no_profile_instrument_function__)) int
no_profile_decl(int value) {
    return value + 1;
}
