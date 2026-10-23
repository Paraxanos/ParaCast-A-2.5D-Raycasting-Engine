#ifndef PC_TEST_H
#define PC_TEST_H

#include <stdio.h>

static int pc_failures;

#define PC_CHECK(cond)                                                          \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            pc_failures++;                                                      \
        }                                                                       \
    } while (0)

#define PC_TEST_RESULT() (printf("%s: %s\n", __FILE__, pc_failures ? "FAILED" : "ok"), pc_failures != 0)

#endif
