typedef int Two[2];
typedef int Three[3];
static Two bounds;
static Three bounds = { 1, 2, 3 };

int main(void) { return bounds[0]; }
