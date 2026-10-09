/* sg_time.h - local-time helpers over epoch milliseconds.
 * Implementation: sg_time.c (card C1). Uses localtime_r / mktime from libc; the
 * zone comes from the process TZ (bionic reads persist.sys.timezone on device;
 * host tests set the TZ env var, e.g. TZ=America/Argentina/Buenos_Aires).
 * All functions are pure, re-entrant, heap-free, and never fail for inputs in
 * [SG_TIME_MIN_MS, SG_TIME_MAX_MS]; out-of-range inputs return SG_TIME_ERR.
 */
#ifndef SG_TIME_H
#define SG_TIME_H

#include <stdint.h>

#define SG_TIME_MIN_MS   INT64_C(0)                 /* 1970-01-01 */
#define SG_TIME_MAX_MS   INT64_C(4102444800000)     /* 2100-01-01 */
#define SG_TIME_ERR      (-1)

/* Broken-down local time. date is yyyymmdd (e.g. 20260109), wday 0=Sunday..6=Saturday,
 * mod = minute of day 0..1439. */
typedef struct {
    int32_t date;
    int32_t mod;
    int8_t  wday;
} SgLocal;

/* Convert epoch ms to local date/minute-of-day. Returns 0 on success, SG_TIME_ERR if
 * ms out of range (out is zeroed in that case). */
int sg_time_local(int64_t ms, SgLocal *out);

/* Epoch ms of local `date` at minute-of-day `mod` (0..1439), DST resolved by mktime
 * (tm_isdst = -1). Returns SG_TIME_ERR on invalid date/mod or conversion failure.
 * For a non-existent local time (DST gap) returns the mktime-normalised instant. */
int64_t sg_time_from_local(int32_t date, int32_t mod);

/* date + days (days may be negative, |days| <= 400). Returns SG_TIME_ERR on bad date. */
int32_t sg_time_date_add(int32_t date, int32_t days);

/* Day-of-week 0..6 for a yyyymmdd date; SG_TIME_ERR on bad date. */
int sg_time_wday(int32_t date);

/* Round minutes DOWN to a multiple of step (step > 0). Negative input -> 0. */
int32_t sg_time_floor_min(int32_t minutes, int32_t step);

/* Round minute-of-day to nearest multiple of step, wrapping at 1440. */
int32_t sg_time_round_mod(int32_t mod, int32_t step);

/* Integer division of (a_ms - b_ms) by 60000 rounded toward zero. Saturates to
 * INT32 range. */
int32_t sg_time_diff_min(int64_t a_ms, int64_t b_ms);

#endif /* SG_TIME_H */
