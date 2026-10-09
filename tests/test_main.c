/* test_main.c - runs every test group; returns non-zero on any failed check. */
#include "sg_test.h"
#include <string.h>

int g_sg_checks = 0;
int g_sg_fail = 0;
int g_sg_tests = 0;

static char g_tz_saved[128];
static int g_tz_was_set = 0;

void sg_tz_restore(void) {
    if (g_tz_was_set) {
        (void)setenv("TZ", g_tz_saved, 1);
    } else {
        (void)unsetenv("TZ");
    }
    tzset();
}

int main(void) {
    const char *tz = getenv("TZ");
    if (tz != NULL) {
        g_tz_was_set = 1;
        (void)snprintf(g_tz_saved, sizeof g_tz_saved, "%s", tz);
    }
    run_time_tests();
    run_sleeplog_tests();
    run_store_tests();
    run_core_tests();
    sg_tz_restore();
    printf("%d tests, %d checks, %d failed\n", g_sg_tests, g_sg_checks, g_sg_fail);
    return g_sg_fail == 0 ? 0 : 1;
}
