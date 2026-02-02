#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: ...\n");
        return EXIT_FAILURE;
    }

    printf("Compute manager started\n");

    // Parse arguments
    // char* endptr;
    // g_ppid = (int)strtol(argv[1], &endptr, 10);
    // if (*endptr != '\0') return EXIT_FAILURE;

    // g_interval = (int)strtol(argv[2], &endptr, 10);
    // if (*endptr != '\0') return EXIT_FAILURE;

    return 0;
}