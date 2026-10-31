static const char *runtime_pointer;
static const char *bad[2][2] = {{"ok", runtime_pointer}, {"still-ok", "fine"}};
int main(void) { return bad[0][0][0]; }
