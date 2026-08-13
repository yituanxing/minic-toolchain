typedef struct Left { int value; } Left;
typedef struct Right { int value; } Right;
typedef struct Holder { Left left; } Holder;

static Holder holder = { (Right) { 1 } };

int main(void) { return 0; }
