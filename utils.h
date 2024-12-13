#include <stdio.h>

#define DIE(condition, explanation)                                             \
    do {                                                                        \
        if (condition) {                                                        \
            fprintf(stderr, "(%d, %s): %s\n", __LINE__, __FILE__, explanation); \
            exit(1);                                                            \
        }                                                                       \
    } while (0)

