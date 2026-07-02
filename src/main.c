#include "../include/alshpp.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input.alsh>\n", argv[0]);
        return 1;
    }

    char *output = alshpp_preprocess_file(argv[1]);
    if (!output) {
        fprintf(stderr, "alshpp: failed to preprocess '%s'\n", argv[1]);
        return 1;
    }

    fputs(output, stdout);
    alshpp_free_output(output);
    return 0;
}