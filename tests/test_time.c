/* test_time.c - sg_time.h semantics (local time, date arithmetic, rounding). */
#include "sg_test.h"

#define TZ_BA  "America/Argentina/Buenos_Aires"
#define TZ_MAD "Europe/Madrid"

static void test_time_roundtrip_day_edges(void) {
    SgLocal l;
    int64_t a, b;
    sg_tz_set(TZ_BA);
    a = sg_at(20261009, 0);
    SG_CHECK(sg_time_local(a, &l) == 0);
    SG_CHECK(l.date == 20261009 && l.mod == 0 && l.wday == 5);
    b = sg_at(20261009, 1439);
    SG_CHECK(sg_time_local(b, &l) == 0);
    SG_CHECK(l.date == 20261009 && l.mod == 1439 && l.wday == 5);
    SG_CHECK(b - a == SG_MIN_TO_MS(1439));
    SG_CHECK(sg_time_local(b + SG_MS_PER_MIN, &l) == 0);
    SG_CHECK(l.date == 20261010 && l.mod == 0 && l.wday == 6);
    SG_CHECK(sg_time_local(a - SG_MS_PER_MIN, &l) == 0);
    SG_CHECK(l.date == 20261008 && l.mod == 1439 && l.wday == 4);
}

static void test_time_local_out_of_range(void) {
    SgLocal l;
    l.date = 1;
    l.mod = 1;
    l.wday = 1;
    SG_CHECK(sg_time_local(-1, &l) == SG_TIME_ERR);
    SG_CHECK(l.date == 0 && l.mod == 0);
    SG_CHECK(sg_time_local(SG_TIME_MAX_MS + 1, &l) == SG_TIME_ERR);
    SG_CHECK(sg_time_local(SG_TIME_MIN_MS, &l) == 0);
}

static void test_time_wday(void) {
    SgLocal l;
    sg_tz_set(TZ_BA);
    SG_CHECK(sg_time_wday(20261009) == 5);
    SG_CHECK(sg_time_wday(20261010) == 6);
    SG_CHECK(sg_time_wday(20261011) == 0);
    SG_CHECK(sg_time_wday(20261012) == 1);
    SG_CHECK(sg_time_wday(20000101) == 6);
    SG_CHECK(sg_time_wday(20261301) == SG_TIME_ERR);
    /* consistent with the wday reported by sg_time_local */
    SG_CHECK(sg_time_local(sg_at(20261012, 720), &l) == 0 && l.wday == sg_time_wday(20261012));
}

static void test_time_dst_gap_madrid(void) {
    SgLocal l;
    int64_t g;
    sg_tz_set(TZ_MAD);
    /* 2026-03-29 02:30 does not exist in Madrid: result is normalised (03:30 or 01:30) */
    g = sg_at(20260329, 150);
    SG_CHECK(g != SG_TIME_ERR);
    SG_CHECK(sg_time_local(g, &l) == 0);
    SG_CHECK(l.date == 20260329);
    SG_CHECK(l.mod == 210 || l.mod == 90);
    /* the spring-forward day has 23 hours */
    SG_CHECK(sg_at(20260330, 0) - sg_at(20260329, 0) == SG_MIN_TO_MS(23 * 60));
    /* times that exist are unaffected */
    SG_CHECK(sg_time_local(sg_at(20260329, 60), &l) == 0 && l.mod == 60);
    SG_CHECK(sg_time_local(sg_at(20260329, 240), &l) == 0 && l.mod == 240);
}

static void test_time_dst_overlap_madrid(void) {
    SgLocal l;
    int64_t o;
    sg_tz_set(TZ_MAD);
    /* 2026-10-25 02:30 occurs twice; either instant maps back to 02:30 */
    o = sg_at(20261025, 150);
    SG_CHECK(sg_time_local(o, &l) == 0);
    SG_CHECK(l.date == 20261025 && l.mod == 150);
    /* the fall-back day has 25 hours */
    SG_CHECK(sg_at(20261026, 0) - sg_at(20261025, 0) == SG_MIN_TO_MS(25 * 60));
}

static void test_time_no_dst_buenos_aires(void) {
    SgLocal l;
    sg_tz_set(TZ_BA);
    SG_CHECK(sg_at(20261026, 0) - sg_at(20261025, 0) == SG_MIN_TO_MS(24 * 60));
    SG_CHECK(sg_at(20260329, 150) - sg_at(20260329, 0) == SG_MIN_TO_MS(150));
    SG_CHECK(sg_time_local(sg_at(20261025, 150), &l) == 0);
    SG_CHECK(l.date == 20261025 && l.mod == 150);
}

static void test_time_date_add_month_year(void) {
    sg_tz_set(TZ_BA);
    SG_CHECK(sg_time_date_add(20260131, 1) == 20260201);
    SG_CHECK(sg_time_date_add(20261231, 1) == 20270101);
    SG_CHECK(sg_time_date_add(20260301, -1) == 20260228);
    SG_CHECK(sg_time_date_add(20260228, 1) == 20260301);
    SG_CHECK(sg_time_date_add(20240301, -1) == 20240229);
    SG_CHECK(sg_time_date_add(20260101, -1) == 20251231);
    SG_CHECK(sg_time_date_add(20261009, 0) == 20261009);
    SG_CHECK(sg_time_date_add(20261301, 1) == SG_TIME_ERR);
}

static void test_time_date_add_400(void) {
    sg_tz_set(TZ_BA);
    SG_CHECK(sg_time_date_add(20260101, 400) == 20270205);
    SG_CHECK(sg_time_date_add(20260101, -400) == 20241127);
    SG_CHECK(sg_time_date_add(sg_time_date_add(20260101, 400), -400) == 20260101);
    SG_CHECK(sg_time_date_add(20261009, 365) == 20271009);
    SG_CHECK(sg_time_date_add(20280228, 1) == 20280229);
}

static void test_time_floor_round(void) {
    sg_tz_set(TZ_BA);
    SG_CHECK(sg_time_floor_min(0, 15) == 0);
    SG_CHECK(sg_time_floor_min(14, 15) == 0);
    SG_CHECK(sg_time_floor_min(15, 15) == 15);
    SG_CHECK(sg_time_floor_min(100, 30) == 90);
    SG_CHECK(sg_time_floor_min(-5, 15) == 0);
}

static void test_time_round_mod_wrap(void) {
    sg_tz_set(TZ_BA);
    SG_CHECK(sg_time_round_mod(0, 15) == 0);
    SG_CHECK(sg_time_round_mod(7, 15) == 0);
    SG_CHECK(sg_time_round_mod(8, 15) == 15);
    SG_CHECK(sg_time_round_mod(608, 15) == 615);
    SG_CHECK(sg_time_round_mod(1400, 60) == 1380);
    SG_CHECK(sg_time_round_mod(1438, 15) == 0);
    SG_CHECK(sg_time_round_mod(1439, 60) == 0);
}

static void test_time_diff_min_saturation(void) {
    sg_tz_set(TZ_BA);
    SG_CHECK(sg_time_diff_min(SG_MIN_TO_MS(90), 0) == 90);
    SG_CHECK(sg_time_diff_min(0, SG_MIN_TO_MS(90)) == -90);
    SG_CHECK(sg_time_diff_min(89999, 0) == 1);
    SG_CHECK(sg_time_diff_min(-89999, 0) == -1);
    SG_CHECK(sg_time_diff_min(INT64_C(4000000000000000000), 0) == INT32_MAX);
    SG_CHECK(sg_time_diff_min(0, INT64_C(4000000000000000000)) == INT32_MIN);
    SG_CHECK(sg_time_diff_min(INT64_MAX, INT64_MIN) == INT32_MAX);
    SG_CHECK(sg_time_diff_min(INT64_MIN, INT64_MAX) == INT32_MIN);
}

void run_time_tests(void) {
    SG_RUN(test_time_roundtrip_day_edges);
    SG_RUN(test_time_local_out_of_range);
    SG_RUN(test_time_wday);
    SG_RUN(test_time_dst_gap_madrid);
    SG_RUN(test_time_dst_overlap_madrid);
    SG_RUN(test_time_no_dst_buenos_aires);
    SG_RUN(test_time_date_add_month_year);
    SG_RUN(test_time_date_add_400);
    SG_RUN(test_time_floor_round);
    SG_RUN(test_time_round_mod_wrap);
    SG_RUN(test_time_diff_min_saturation);
}
