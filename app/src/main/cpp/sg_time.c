/* sg_time.c - local-time helpers over epoch milliseconds (card C1).
 * Pure, re-entrant, heap-free. Local zone comes from the process TZ.
 */
#define _POSIX_C_SOURCE 200809L

#include "sg_time.h"

#include <limits.h>
#include <string.h>
#include <time.h>

#define SG_TIME_MS_PER_DAY_CONST  INT64_C(86400000)
#define SG_TIME_YEAR_MIN          1970
#define SG_TIME_YEAR_MAX          2100

static int is_leap(int y)
{
    return ((y % 4) == 0 && (y % 100) != 0) || (y % 400) == 0;
}

static int days_in_month(int y, int m)
{
    static const int dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2) {
        return is_leap(y) ? 29 : 28;
    }
    return dim[m - 1];
}

/* 1 if date is yyyymmdd within 1970..2100 and a real calendar day. */
static int valid_date(int32_t date)
{
    int y;
    int m;
    int d;
    if (date <= 0) {
        return 0;
    }
    y = (int)(date / 10000);
    m = (int)((date / 100) % 100);
    d = (int)(date % 100);
    if (y < SG_TIME_YEAR_MIN || y > SG_TIME_YEAR_MAX) {
        return 0;
    }
    if (m < 1 || m > 12) {
        return 0;
    }
    if (d < 1 || d > days_in_month(y, m)) {
        return 0;
    }
    return 1;
}

/* Days since 1970-01-01 for a valid date (proleptic Gregorian, no TZ). */
static int64_t days_from_date(int32_t date)
{
    int64_t y = (int64_t)(date / 10000);
    int64_t m = (int64_t)((date / 100) % 100);
    int64_t d = (int64_t)(date % 100);
    int64_t era;
    int64_t yoe;
    int64_t doy;
    int64_t doe;

    if (m <= 2) {
        y -= 1;
    }
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* Inverse of days_from_date; returns yyyymmdd. */
static int32_t date_from_days(int64_t z)
{
    int64_t era;
    int64_t doe;
    int64_t yoe;
    int64_t y;
    int64_t doy;
    int64_t mp;
    int64_t d;
    int64_t m;

    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) {
        y += 1;
    }
    return (int32_t)(y * 10000 + m * 100 + d);
}

int sg_time_local(int64_t ms, SgLocal *out)
{
    time_t secs;
    struct tm tmv;
    struct tm *r;

    if (out == NULL) {
        return SG_TIME_ERR;
    }
    memset(out, 0, sizeof *out);
    if (ms < SG_TIME_MIN_MS || ms > SG_TIME_MAX_MS) {
        return SG_TIME_ERR;
    }
    secs = (time_t)(ms / INT64_C(1000));
    tzset();
    r = localtime_r(&secs, &tmv);
    if (r == NULL) {
        return SG_TIME_ERR;
    }
    out->date = (int32_t)((tmv.tm_year + 1900) * 10000 + (tmv.tm_mon + 1) * 100 + tmv.tm_mday);
    out->mod = (int32_t)(tmv.tm_hour * 60 + tmv.tm_min);
    out->wday = (int8_t)tmv.tm_wday;
    return 0;
}

int64_t sg_time_from_local(int32_t date, int32_t mod)
{
    struct tm tmv;
    time_t t;
    int64_t ms;

    if (!valid_date(date) || mod < 0 || mod > 1439) {
        return SG_TIME_ERR;
    }
    memset(&tmv, 0, sizeof tmv);
    tmv.tm_year = (int)(date / 10000) - 1900;
    tmv.tm_mon = (int)((date / 100) % 100) - 1;
    tmv.tm_mday = (int)(date % 100);
    tmv.tm_hour = (int)(mod / 60);
    tmv.tm_min = (int)(mod % 60);
    tmv.tm_sec = 0;
    tmv.tm_isdst = -1;
    tzset();
    t = mktime(&tmv);
    if (t == (time_t)-1) {
        return SG_TIME_ERR;
    }
    ms = (int64_t)t * INT64_C(1000);
    if (ms < SG_TIME_MIN_MS || ms > SG_TIME_MAX_MS) {
        return SG_TIME_ERR;
    }
    return ms;
}

int32_t sg_time_date_add(int32_t date, int32_t days)
{
    int64_t base;
    int64_t target;
    int32_t out;

    if (!valid_date(date) || days > 400 || days < -400) {
        return SG_TIME_ERR;
    }
    /* Calendar arithmetic only: no TZ involved, so no DST gap can shift the date. */
    base = days_from_date(date);
    target = base + (int64_t)days;
    out = date_from_days(target);
    if (!valid_date(out)) {
        return SG_TIME_ERR;
    }
    return out;
}

int sg_time_wday(int32_t date)
{
    int64_t d;
    int64_t w;

    if (!valid_date(date)) {
        return SG_TIME_ERR;
    }
    d = days_from_date(date);
    /* 1970-01-01 was a Thursday (wday 4). */
    w = (d + 4) % 7;
    if (w < 0) {
        w += 7;
    }
    return (int)w;
}

int32_t sg_time_floor_min(int32_t minutes, int32_t step)
{
    if (minutes <= 0 || step <= 0) {
        return 0;
    }
    return (minutes / step) * step;
}

int32_t sg_time_round_mod(int32_t mod, int32_t step)
{
    int64_t m;
    int64_t s;
    int64_t r;

    m = (int64_t)mod % 1440;
    if (m < 0) {
        m += 1440;
    }
    if (step <= 0) {
        return (int32_t)m;
    }
    s = (int64_t)step;
    r = ((m + s / 2) / s) * s;
    r %= 1440;
    return (int32_t)r;
}

int32_t sg_time_diff_min(int64_t a_ms, int64_t b_ms)
{
    int64_t d;
    int64_t q;

    if (b_ms > 0 && a_ms < INT64_MIN + b_ms) {
        d = INT64_MIN;
    } else if (b_ms < 0 && a_ms > INT64_MAX + b_ms) {
        d = INT64_MAX;
    } else {
        d = a_ms - b_ms;
    }
    /* C integer division truncates toward zero, as specified. */
    q = d / INT64_C(60000);
    if (q > INT32_MAX) {
        return INT32_MAX;
    }
    if (q < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)q;
}
