extern int first[];
extern double second[];
int wrong_elements(void) { return &first != &second; }
