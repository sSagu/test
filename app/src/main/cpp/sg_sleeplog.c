/* sg_sleeplog.c - F4 sleep log ring buffer, weekly debt, F5 weekday reference (C1). */
#include "sg_sleeplog.h"

#include <stddef.h>

#include "sg_config.h"
#include "sg_time.h"

#define SG_LOG_TWELVE_H_MS INT64_C(43200000)

static int32_t idx_of(int i, uint16_t head)
{
    return (int32_t)(((int)head + i) % SG_LOG_RETAIN_NIGHTS);
}

/* Index in nights[] of the newest record, or -1 when the log is empty. */
static int newest_index(const SgState *s)
{
    if (s->night_count == 0) {
        return -1;
    }
    return (int)idx_of((int)s->night_count - 1, s->night_head);
}

static void append_night(SgState *s, int64_t bed_ms, int64_t wake_ms)
{
    int slot;
    if (s->night_count < SG_LOG_RETAIN_NIGHTS) {
        slot = (int)idx_of((int)s->night_count, s->night_head);
        s->night_count = (uint16_t)(s->night_count + 1);
    } else {
        slot = (int)s->night_head;
        s->night_head = (uint16_t)((s->night_head + 1) % SG_LOG_RETAIN_NIGHTS);
    }
    s->nights[slot].bed_ms = bed_ms;
    s->nights[slot].wake_ms = wake_ms;
    s->nights[slot].closed = 0;
}

int sg_log_bed_tap(SgState *s, int64_t now_ms, int64_t T_ms)
{
    int newest;
    SgNight *n;
    int64_t gap;
    int64_t new_bed;
    int changed;

    if (s == NULL || now_ms <= 0) {
        return 0;
    }
    if (T_ms < 0) {
        T_ms = 0;
    }
    newest = newest_index(s);
    if (newest >= 0) {
        n = &s->nights[newest];
        gap = now_ms - n->bed_ms;
        if (n->closed == 0 && gap <= SG_MIN_TO_MS(SG_TAP_DEDUP_MIN) &&
            gap >= -SG_MIN_TO_MS(SG_TAP_DEDUP_MIN)) {
            new_bed = (now_ms > n->bed_ms) ? now_ms : n->bed_ms;
            changed = (new_bed != n->bed_ms) || (n->wake_ms != T_ms);
            n->bed_ms = new_bed;
            n->wake_ms = T_ms;
            return changed ? 2 : 0;
        }
    }
    append_night(s, now_ms, T_ms);
    return 1;
}

int64_t sg_log_open_bed_for(const SgState *s, int64_t T_ms)
{
    int newest;
    const SgNight *n;

    if (s == NULL || T_ms <= 0) {
        return 0;
    }
    newest = newest_index(s);
    if (newest < 0) {
        return 0;
    }
    n = &s->nights[newest];
    if (n->closed != 0 || n->wake_ms != T_ms) {
        return 0;
    }
    return n->bed_ms;
}

int sg_log_bed_retap(SgState *s, int64_t now_ms, int64_t T_ms)
{
    int64_t open_bed;
    int newest;
    SgNight *n;

    if (s == NULL || now_ms <= 0) {
        return 0;
    }
    open_bed = sg_log_open_bed_for(s, T_ms);
    if (open_bed <= 0) {
        return sg_log_bed_tap(s, now_ms, T_ms);
    }
    newest = newest_index(s);
    if (newest < 0) {
        return 0;
    }
    n = &s->nights[newest];
    if (now_ms <= n->bed_ms) {
        return 0;
    }
    n->bed_ms = now_ms;
    return 2;
}

void sg_log_follow_alarm(SgState *s, int64_t T_ms)
{
    int newest;
    if (s == NULL) {
        return;
    }
    newest = newest_index(s);
    if (newest < 0 || s->nights[newest].closed != 0) {
        return;
    }
    /* A T more than SG_MAX_OPPORTUNITY_MIN after bed belongs to a later night (alarm
     * dismissed before it rang): keep this night's wake time. */
    if (T_ms > 0 && T_ms - s->nights[newest].bed_ms > SG_MIN_TO_MS(SG_MAX_OPPORTUNITY_MIN)) {
        return;
    }
    s->nights[newest].wake_ms = (T_ms < 0) ? 0 : T_ms;
}

int sg_log_close_if_due(SgState *s, int64_t now_ms)
{
    int i;
    int closed = 0;
    SgNight *n;

    if (s == NULL) {
        return 0;
    }
    for (i = 0; i < (int)s->night_count; i++) {
        n = &s->nights[idx_of(i, s->night_head)];
        if (n->closed == 0 && n->wake_ms > 0 && now_ms >= n->wake_ms) {
            n->closed = 1;
            closed = 1;
        }
    }
    return closed;
}

const SgNight *sg_log_at(const SgState *s, int i)
{
    if (s == NULL || i < 0 || i >= (int)s->night_count) {
        return NULL;
    }
    return &s->nights[idx_of(i, s->night_head)];
}

int32_t sg_log_est_sleep_min(const SgNight *n)
{
    int32_t opp;
    int32_t est;

    if (n == NULL || n->wake_ms <= 0) {
        return -1;
    }
    opp = sg_time_diff_min(n->wake_ms, n->bed_ms);
    if (opp < 0) {
        opp = 0;
    }
    if (opp > SG_MAX_OPPORTUNITY_MIN) {
        opp = SG_MAX_OPPORTUNITY_MIN;
    }
    est = opp - SG_LATENCY_MIN;
    if (est < 0) {
        est = 0;
    }
    return est;
}

/* Instant a record is attributed to (the sg_log_week rule): wake when known, else bed + 12 h. */
static int64_t attr_ms_of(const SgNight *n)
{
    return (n->wake_ms > 0) ? n->wake_ms : n->bed_ms + SG_LOG_TWELVE_H_MS;
}

static void empty_view(SgNightView *v, int32_t date, int8_t wday)
{
    v->date = date;
    v->wday = wday;
    v->status = 0;
    v->bed_mod = -1;
    v->wake_mod = -1;
    v->est_sleep_min = -1;
}

void sg_log_week(const SgState *s, int32_t today_date, SgWeek *out)
{
    int i;
    int k;
    int32_t date;
    int32_t wd;
    int32_t debt_sum = 0;
    SgLocal loc;
    const SgNight *n;

    if (out == NULL) {
        return;
    }
    out->debt_min = 0;
    out->logged_count = 0;
    for (k = 0; k < SG_DEBT_WINDOW_NIGHTS; k++) {
        date = sg_time_date_add(today_date, k - (SG_DEBT_WINDOW_NIGHTS - 1));
        wd = sg_time_wday(date);
        if (date == SG_TIME_ERR || wd == SG_TIME_ERR) {
            empty_view(&out->night[k], 0, 0);
        } else {
            empty_view(&out->night[k], date, (int8_t)wd);
        }
    }
    if (s == NULL || today_date == 0 || sg_time_date_add(today_date, 0) == SG_TIME_ERR) {
        return;
    }

    /* Oldest record first; a later (newer) record overwrites an earlier one for the
     * same date, so the newest record wins. */
    for (i = 0; i < (int)s->night_count; i++) {
        SgNightView *v = NULL;
        int64_t attr_ms;
        int32_t est;

        n = sg_log_at(s, i);
        if (n == NULL) {
            continue;
        }
        attr_ms = attr_ms_of(n);
        if (sg_time_local(attr_ms, &loc) != 0) {
            continue;
        }
        for (k = 0; k < SG_DEBT_WINDOW_NIGHTS; k++) {
            if (out->night[k].date == loc.date) {
                v = &out->night[k];
                break;
            }
        }
        if (v == NULL) {
            continue;
        }
        if (n->wake_ms > 0) {
            v->status = 1;
            est = sg_log_est_sleep_min(n);
            v->est_sleep_min = (int16_t)est;
            if (sg_time_local(n->wake_ms, &loc) == 0) {
                v->wake_mod = (int16_t)loc.mod;
            } else {
                v->wake_mod = -1;
            }
        } else {
            v->status = 2;
            v->est_sleep_min = -1;
            v->wake_mod = -1;
        }
        if (sg_time_local(n->bed_ms, &loc) == 0) {
            v->bed_mod = (int16_t)loc.mod;
        } else {
            v->bed_mod = -1;
        }
    }

    for (k = 0; k < SG_DEBT_WINDOW_NIGHTS; k++) {
        if (out->night[k].status == 1) {
            out->logged_count = out->logged_count + 1;
            debt_sum = debt_sum + ((int32_t)s->target_sleep_min - (int32_t)out->night[k].est_sleep_min);
        }
    }
    out->debt_min = (debt_sum < 0) ? 0 : debt_sum;
}

/* ---- v3 manual night (docs/ADVICE-v3.md section 2) ---- */

int32_t sg_log_attr_date(const SgNight *n)
{
    SgLocal loc;

    if (n == NULL || sg_time_local(attr_ms_of(n), &loc) != 0) {
        return 0;
    }
    return loc.date;
}

/* Logical index (oldest = 0) of the NEWEST record attributed to date, or -1. */
static int newest_for_date(const SgState *s, int32_t date)
{
    int i;

    for (i = (int)s->night_count - 1; i >= 0; i--) {
        if (sg_log_attr_date(sg_log_at(s, i)) == date) {
            return i;
        }
    }
    return -1;
}

int sg_log_put_manual(SgState *s, int32_t date, int32_t minutes)
{
    int64_t wake;
    int64_t bed;
    SgLocal loc;
    SgNight *slot;
    int i;
    int count;
    int p;

    if (s == NULL || minutes < SG_MANUAL_MIN_MIN || minutes > SG_MANUAL_MAX_MIN) {
        return -1;
    }
    wake = sg_time_from_local(date, SG_MANUAL_WAKE_MOD);
    if (wake <= 0 || sg_time_local(wake, &loc) != 0 || loc.date != date) {
        return -1;
    }
    bed = wake - SG_MIN_TO_MS((int64_t)minutes + SG_LATENCY_MIN);
    if (bed <= 0) {
        return -1;
    }

    i = newest_for_date(s, date);
    if (i >= 0) {
        slot = &s->nights[idx_of(i, s->night_head)];
        slot->bed_ms = bed;
        slot->wake_ms = wake;
        slot->closed = 1;
        return 2;
    }

    if (s->night_count >= SG_LOG_RETAIN_NIGHTS) {
        s->night_head = (uint16_t)((s->night_head + 1) % SG_LOG_RETAIN_NIGHTS);
        s->night_count = (uint16_t)(s->night_count - 1);
    }
    count = (int)s->night_count;
    p = count;
    if (count > 0 && s->nights[idx_of(count - 1, s->night_head)].closed == 0) {
        p = count - 1; /* an open record always stays the newest */
    }
    while (p > 0 && attr_ms_of(sg_log_at(s, p - 1)) > wake) {
        p--;
    }
    for (i = count; i > p; i--) {
        s->nights[idx_of(i, s->night_head)] = s->nights[idx_of(i - 1, s->night_head)];
    }
    slot = &s->nights[idx_of(p, s->night_head)];
    slot->bed_ms = bed;
    slot->wake_ms = wake;
    slot->closed = 1;
    s->night_count = (uint16_t)(count + 1);
    return 1;
}

void sg_log_clear(SgState *s)
{
    int i;
    if (s == NULL) {
        return;
    }
    for (i = 0; i < SG_LOG_RETAIN_NIGHTS; i++) {
        s->nights[i].bed_ms = 0;
        s->nights[i].wake_ms = 0;
        s->nights[i].closed = 0;
    }
    s->night_count = 0;
    s->night_head = 0;
    sg_ref_clear(s);
}

void sg_ref_add(SgState *s, int16_t mod)
{
    int slot;

    if (s == NULL || mod < 0 || mod > 1439) {
        return;
    }
    if (s->ref_count < SG_REF_SAMPLES_MAX) {
        slot = ((int)s->ref_head + (int)s->ref_count) % SG_REF_SAMPLES_MAX;
        s->ref_count = (uint8_t)(s->ref_count + 1);
    } else {
        slot = (int)s->ref_head;
        s->ref_head = (uint8_t)((s->ref_head + 1) % SG_REF_SAMPLES_MAX);
    }
    s->ref_mod[slot] = mod;
}

int32_t sg_ref_median(const SgState *s)
{
    int16_t tmp[SG_REF_SAMPLES_MAX];
    int i;
    int j;
    int n;

    if (s == NULL || s->ref_count < SG_REF_SAMPLES_MIN) {
        return -1;
    }
    n = (int)s->ref_count;
    for (i = 0; i < n; i++) {
        tmp[i] = s->ref_mod[((int)s->ref_head + i) % SG_REF_SAMPLES_MAX];
    }
    /* insertion sort, n <= 10 */
    for (i = 1; i < n; i++) {
        int16_t key = tmp[i];
        j = i - 1;
        while (j >= 0 && tmp[j] > key) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = key;
    }
    return (int32_t)tmp[(n - 1) / 2];
}

void sg_ref_clear(SgState *s)
{
    int i;
    if (s == NULL) {
        return;
    }
    for (i = 0; i < SG_REF_SAMPLES_MAX; i++) {
        s->ref_mod[i] = 0;
    }
    s->ref_count = 0;
    s->ref_head = 0;
}
