enum KnownIndices { INDEX_0, INDEX_1, INDEX_2, INDEX_3,
                    INDEX_4, INDEX_5, INDEX_6, INDEX_7 };
int touch_index(void) { return INDEX_0; }
enum NewIndices { INDEX_8, INDEX_9 };
struct Pair { int first; int second; };
extern int runtime_index;
/* A speculative bound probe must still reject a nonconstant designator. */
struct Pair invalid[] = { [runtime_index] = {1, 2} };
