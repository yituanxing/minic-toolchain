typedef __signed__ char signed_char_alias;
typedef __signed short signed_short_alias;
typedef __signed__ int signed_int_alias;
typedef __signed long signed_long_alias;

signed_long_alias widen_signed_aliases(signed_char_alias a,
                                       signed_short_alias b,
                                       signed_int_alias c) {
    return (signed_long_alias)a + (signed_long_alias)b + (signed_long_alias)c;
}
