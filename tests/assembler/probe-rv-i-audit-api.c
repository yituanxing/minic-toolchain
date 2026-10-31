#include "minias.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Small independent caller for the legacy explicit Base-I byte audit.
 * The public -march API intentionally does not enable this audit. */
int main(int argc, char **argv) {
    unsigned xlen;
    if (argc != 4 ||
        (strcmp(argv[3], "32") != 0 && strcmp(argv[3], "64") != 0)) {
        fprintf(stderr, "usage: rv-i-audit INPUT OUTPUT 32|64\n");
        return 2;
    }
    xlen = (unsigned)strtoul(argv[3], NULL, 10);
    return minias_assemble_file_target_isa(argv[1], argv[2],
                                           xlen == 32U, 0U, xlen, stderr);
}
