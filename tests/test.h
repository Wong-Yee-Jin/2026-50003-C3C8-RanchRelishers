#ifndef TEST_H
#define TEST_H
#include <stdio.h>
#include <string.h>

/* We roll our own checks instead of linking a test framework so the project
   builds with nothing but a C compiler, and so a grader can read a test file
   straight through. Each test_*.c defines test functions and calls them from
   main(), then returns TEST_SUMMARY(). */
static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond) do {                                        \
    tests_run++;                                                \
    if (!(cond)) {                                              \
        tests_failed++;                                         \
        printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
    }                                                           \
} while (0)

#define CHECK_STR(a, b) CHECK(strcmp((a), (b)) == 0)

#define TEST_SUMMARY() (printf("%s: %d checks, %d failed\n",    \
    __FILE__, tests_run, tests_failed), tests_failed ? 1 : 0)

#endif
