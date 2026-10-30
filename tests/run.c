#include "tests.h"

int g_failures = 0;
int g_checks = 0;

typedef struct {
    const char *name;
    void (*fn)(void);
} TestCase;

int main(void) {
    const TestCase tests[] = {
        {"util", test_util},
        {"json", test_json},
        {"schedule", test_schedule},
        {"stats", test_stats},
        {"store", test_store},
        {"api", test_api},
        {"quit", test_quit},
    };
    for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        printf("== %s\n", tests[i].name);
        tests[i].fn();
    }
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
