/* sg_test.h - minimal host test harness (no framework, no malloc). */
#ifndef SG_TEST_H
#define SG_TEST_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "sg_config.h"
#include "sg_time.h"
#include "sg_state.h"
#include "sg_cmd.h"
#include "sg_sleeplog.h"
#include "sg_store.h"
#include "sg_core.h"

extern int g_sg_checks;
extern int g_sg_fail;
extern int g_sg_tests;

#define SG_CHECK(cond)                                                       \
    do {                                                                     \
        g_sg_checks++;                                                       \
        if (!(cond)) {                                                       \
            g_sg_fail++;                                                     \
            fprintf(stderr, "    CHECK failed %s:%d: %s\n", __FILE__,        \
                    __LINE__, #cond);                                        \
        }                                                                    \
    } while (0)

/* Restores the TZ environment saved by test_main (called after each test). */
void sg_tz_restore(void);

#define SG_RUN(fn)                                                           \
    do {                                                                     \
        int sg_before_ = g_sg_fail;                                          \
        fn();                                                                \
        sg_tz_restore();                                                     \
        g_sg_tests++;                                                        \
        printf("%-40s %s\n", #fn, g_sg_fail == sg_before_ ? "ok" : "FAIL");  \
    } while (0)

/* Explicit zone for tests that depend on local time. */
static inline void sg_tz_set(const char *tz) {
    (void)setenv("TZ", tz, 1);
    tzset();
}

/* Epoch ms of local date (yyyymmdd) at minute-of-day mod. */
static inline int64_t sg_at(int32_t date, int32_t mod) {
    return sg_time_from_local(date, mod);
}

void run_time_tests(void);
void run_sleeplog_tests(void);
void run_store_tests(void);
void run_core_tests(void);

#endif /* SG_TEST_H */
