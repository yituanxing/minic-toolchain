/* Cross-GNU/QEMU oracle for pointer offsets with record_size > signed imm12. */
#include <stdio.h>
typedef struct { unsigned char payload[4096]; } LargeRecord;
static LargeRecord records[4];
int main(int argc, char **argv)
{
    unsigned int i = (unsigned int)(argc & 1);
    (void)argv;
    records[i].payload[17] = 41;
    records[i + 1U].payload[18] = 1;
    printf("%u\n", (unsigned int)records[i].payload[17] +
                    (unsigned int)records[i + 1U].payload[18]);
    return 0;
}
