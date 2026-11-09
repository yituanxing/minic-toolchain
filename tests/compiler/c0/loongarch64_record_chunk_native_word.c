/* Whole aligned LP64D record chunks are machine words; short packed tails
 * retain byte-exact ingress/egress without an out-of-bounds full-width load. */
struct Word { unsigned long long value; };
struct Pair { unsigned long long first, second; };
struct Tail { unsigned char bytes[7]; };

struct Word loong_word_echo(struct Word arg) { return arg; }
struct Pair loong_pair_echo(struct Pair arg) { return arg; }
struct Tail loong_tail_echo(struct Tail arg) { return arg; }
