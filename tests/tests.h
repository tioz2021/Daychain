#ifndef HABITS_TESTS_H
#define HABITS_TESTS_H

#include <stdio.h>
#include <string.h>

extern int g_failures;
extern int g_checks;

#define CHECK(cond)                                                       \
    do {                                                                  \
        g_checks++;                                                       \
        if (!(cond)) {                                                    \
            fprintf(stderr, "    FAIL %s:%d: %s\n", __FILE__, __LINE__,    \
                    #cond);                                               \
            g_failures++;                                                 \
        }                                                                 \
    } while (0)

#define CHECK_STR(a, b)                                                   \
    do {                                                                  \
        g_checks++;                                                       \
        const char *a_ = (a);                                             \
        const char *b_ = (b);                                             \
        if (!a_ || !b_ || strcmp(a_, b_) != 0) {                          \
            fprintf(stderr, "    FAIL %s:%d: \"%s\" != \"%s\"\n",          \
                    __FILE__, __LINE__, a_ ? a_ : "(null)",               \
                    b_ ? b_ : "(null)");                                  \
            g_failures++;                                                 \
        }                                                                 \
    } while (0)

void test_util(void);
void test_json(void);
void test_schedule(void);
void test_stats(void);
void test_store(void);
void test_quit(void);

#endif
