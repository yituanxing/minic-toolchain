typedef int fixed_table_t[2];
static const fixed_table_t too_many = { 1, 2, 3 };
int main(void) { return too_many[0]; }
