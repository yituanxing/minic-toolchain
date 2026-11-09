extern int *first[];
extern const int *second[];
int wrong_nested_qualification(void) { return &first != &second; }
