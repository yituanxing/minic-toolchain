extern int first[2];
extern int second[3];
int wrong_extents(void) { return &first != &second; }
