/* A declaration of an opaque enum inside a function-pointer signature is
 * representable, but materializing an object of that incomplete C type
 * must remain a hard source-language failure, not a guessed integer ABI. */
enum no_integer_abi;
enum no_integer_abi illegal_object;
