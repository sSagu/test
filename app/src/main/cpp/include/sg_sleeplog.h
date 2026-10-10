/* sg_sleeplog.h - F4 sleep log ring buffer, weekly debt, F5 weekday reference.
 * Implementation: sg_sleeplog.c (card C1). Pure functions over SgState, heap-free.
 * Semantics from docs/ADVICE-features.md F4/F5.
 */
#ifndef SG_SLEEPLOG_H
#define SG_SLEEPLOG_H

#include <stdint.h>
#include "sg_state.h"

/* Record a bed tap at now_ms with current relevant alarm T (0 = none).
 * Rules: if the newest record is open (closed==0) and |now - bed| <= SG_TAP_DEDUP_MIN,
 * update bed_ms = max(bed_ms, now) and wake_ms = T; else append a new record
 * {now, T, 0}, evicting the oldest when full. Returns 1 if a record was added,
 * 2 if updated, 0 if nothing changed. Caller enforces the validity window
 * (sg_core does). */
int sg_log_bed_tap(SgState *s, int64_t now_ms, int64_t T_ms);

/* v2. bed_ms of the newest record if it is open (closed == 0) and its wake_ms == T_ms
 * with T_ms > 0 ("this night is already logged for alarm T"); otherwise 0. NULL -> 0. */
int64_t sg_log_open_bed_for(const SgState *s, int64_t T_ms);

/* v2 bed tap used by sg_core for BOTH the in-app button and the notification action.
 * If sg_log_open_bed_for(s, T_ms) > 0: set that record's bed_ms = max(bed_ms, now_ms),
 * leave wake_ms/closed unchanged, return 2 if bed_ms changed else 0 (never appends, at
 * any distance in time: this is the "Actualizar" re-tap). Otherwise: exactly
 * sg_log_bed_tap(s, now_ms, T_ms). NULL or now_ms <= 0 -> 0. */
int sg_log_bed_retap(SgState *s, int64_t now_ms, int64_t T_ms);

/* Make the open record (if any) follow a changed T (0 = alarm removed -> wake 0). */
void sg_log_follow_alarm(SgState *s, int64_t T_ms);

/* Close the open record if now_ms >= wake_ms > 0. Returns 1 if closed. */
int sg_log_close_if_due(SgState *s, int64_t now_ms);

/* Pointer to i-th record oldest-first (0 <= i < night_count) or NULL. */
const SgNight *sg_log_at(const SgState *s, int i);

/* Estimated sleep minutes for a record: opportunity = wake - bed clamped to
 * [0, SG_MAX_OPPORTUNITY_MIN]; est = max(0, opportunity - SG_LATENCY_MIN).
 * Returns -1 if wake_ms == 0 (unknown). */
int32_t sg_log_est_sleep_min(const SgNight *n);

/* Per-night view for the UI: the 7 local calendar dates ending at today_date
 * (today_date = local date of now). status: 0 no record, 1 record with wake,
 * 2 record without wake. A night is attributed to the local date of wake_ms
 * (or of bed_ms + 12 h when wake unknown). Newest record wins per date. */
typedef struct {
    int32_t date;        /* yyyymmdd */
    int8_t  wday;
    uint8_t status;
    int16_t bed_mod;     /* minute-of-day, -1 if none */
    int16_t wake_mod;    /* -1 if none */
    int16_t est_sleep_min; /* -1 if none */
} SgNightView;

typedef struct {
    SgNightView night[SG_DEBT_WINDOW_NIGHTS]; /* oldest first, [6] = today */
    int32_t debt_min;      /* max(0, sum(target - est)) over status==1 nights */
    int32_t logged_count;  /* nights with status==1 */
} SgWeek;

void sg_log_week(const SgState *s, int32_t today_date, SgWeek *out);

/* Erase all nights and reference samples. */
void sg_log_clear(SgState *s);

/* F5 reference: add a weekday (Mon-Fri wake date) sample minute-of-day. */
void sg_ref_add(SgState *s, int16_t mod);
/* Median of samples (lower median for even counts); -1 if ref_count < SG_REF_SAMPLES_MIN. */
int32_t sg_ref_median(const SgState *s);
void sg_ref_clear(SgState *s);

#endif /* SG_SLEEPLOG_H */
