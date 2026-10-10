/* sg_state.c - factory defaults and sanitisation of SgState (card C1). */
#include "sg_state.h"

#include <stddef.h>

#include "sg_time.h"

#define SG_STATE_YEAR_MIN 1970
#define SG_STATE_YEAR_MAX 2100

static void clear_nights(SgState *s)
{
    int i;
    for (i = 0; i < SG_LOG_RETAIN_NIGHTS; i++) {
        s->nights[i].bed_ms = 0;
        s->nights[i].wake_ms = 0;
        s->nights[i].closed = 0;
    }
    s->night_count = 0;
    s->night_head = 0;
}

static void clear_refs(SgState *s)
{
    int i;
    for (i = 0; i < SG_REF_SAMPLES_MAX; i++) {
        s->ref_mod[i] = 0;
    }
    s->ref_count = 0;
    s->ref_head = 0;
}

void sg_state_defaults(SgState *s)
{
    uint8_t f = 0;

    if (s == NULL) {
        return;
    }
    if (SG_ENABLED_DEFAULT) {
        f |= SG_FLAG_ENABLED;
    }
    if (SG_WINDDOWN_DEFAULT_ON) {
        f |= SG_FLAG_WINDDOWN_ON;
    }
    if (SG_LATE_DEFAULT_ON) {
        f |= SG_FLAG_LATE_ON;
    }
    if (SG_JETLAG_DEFAULT_ON) {
        f |= SG_FLAG_JETLAG_ON;
    }
    if (SG_JETLAG_NOALARM_DEFAULT_ON) {
        f |= SG_FLAG_JETLAG_NOALARM;
    }
    if (SG_ONLY_CLOCK_DEFAULT) {
        f |= SG_FLAG_ONLY_CLOCK;
    }
    s->flags = f;
    s->snooze_count = 0;
    s->lead_min = (uint16_t)SG_LEAD_DEFAULT_MIN;
    s->target_sleep_min = (uint16_t)SG_TARGET_SLEEP_DEFAULT_MIN;
    s->winddown_min = (uint16_t)SG_WINDDOWN_DEFAULT_MIN;

    s->last_seen_T = 0;
    s->last_f1_for_T = 0;
    s->last_f2_for_T = 0;
    s->last_f5_for_T = 0;
    s->last_notified_ms = 0;
    s->debounce_due_ms = 0;
    s->debounce_T = 0;
    s->last_f5_date = 0;
    s->boot_unseen = 0;

    clear_nights(s);
    clear_refs(s);
}

/* Snap v into [lo, hi] on the grid lo + k*step. Returns the corrected value.
 * All ranges in sg_config.h have (hi - lo) divisible by step. */
static int32_t snap(int32_t v, int32_t lo, int32_t hi, int32_t step)
{
    int32_t r;
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    r = (v - lo) % step;
    if (r == 0) {
        return v;
    }
    if (r * 2 >= step) {
        v = v + (step - r);
    } else {
        v = v - r;
    }
    if (v > hi) {
        v = hi;
    }
    return v;
}

static int valid_ms(int64_t ms)
{
    return ms >= SG_TIME_MIN_MS && ms <= SG_TIME_MAX_MS;
}

static int valid_date_field(int32_t date)
{
    int32_t y;
    int32_t m;
    int32_t d;
    if (date == 0) {
        return 1;
    }
    y = date / 10000;
    m = (date / 100) % 100;
    d = date % 100;
    return y >= SG_STATE_YEAR_MIN && y <= SG_STATE_YEAR_MAX && m >= 1 && m <= 12 &&
           d >= 1 && d <= 31;
}

int sg_state_sanitize(SgState *s)
{
    int fixed = 0;
    int32_t v;
    int i;
    int16_t mod;

    if (s == NULL) {
        return 0;
    }

    /* settings */
    v = snap((int32_t)s->lead_min, SG_LEAD_MIN_MIN, SG_LEAD_MAX_MIN, SG_LEAD_STEP_MIN);
    if (v != (int32_t)s->lead_min) {
        s->lead_min = (uint16_t)v;
        fixed++;
    }
    v = snap((int32_t)s->target_sleep_min, SG_TARGET_SLEEP_MIN_MIN, SG_TARGET_SLEEP_MAX_MIN,
             SG_TARGET_SLEEP_STEP_MIN);
    if (v != (int32_t)s->target_sleep_min) {
        s->target_sleep_min = (uint16_t)v;
        fixed++;
    }
    v = snap((int32_t)s->winddown_min, SG_WINDDOWN_MIN_MIN, SG_WINDDOWN_MAX_MIN,
             SG_WINDDOWN_STEP_MIN);
    if (v != (int32_t)s->winddown_min) {
        s->winddown_min = (uint16_t)v;
        fixed++;
    }
    if (s->snooze_count > SG_SNOOZE_MAX) {
        s->snooze_count = SG_SNOOZE_MAX;
        fixed++;
    }

    /* scheduler memory: out-of-range epoch values become 0 (none) */
    if (!valid_ms(s->last_seen_T) && s->last_seen_T != 0) {
        s->last_seen_T = 0;
        fixed++;
    }
    if (s->last_seen_T < 0) {
        s->last_seen_T = 0;
    }
    if (!valid_ms(s->last_f1_for_T) && s->last_f1_for_T != 0) {
        s->last_f1_for_T = 0;
        fixed++;
    }
    if (!valid_ms(s->last_f2_for_T) && s->last_f2_for_T != 0) {
        s->last_f2_for_T = 0;
        fixed++;
    }
    if (!valid_ms(s->last_f5_for_T) && s->last_f5_for_T != 0) {
        s->last_f5_for_T = 0;
        fixed++;
    }
    if (!valid_ms(s->last_notified_ms) && s->last_notified_ms != 0) {
        s->last_notified_ms = 0;
        fixed++;
    }
    if (!valid_ms(s->debounce_due_ms) && s->debounce_due_ms != 0) {
        s->debounce_due_ms = 0;
        fixed++;
    }
    if (!valid_ms(s->debounce_T) && s->debounce_T != 0) {
        s->debounce_T = 0;
        fixed++;
    }
    if (s->debounce_due_ms == 0 && s->debounce_T != 0) {
        s->debounce_T = 0;
        fixed++;
    }
    if (!valid_date_field(s->last_f5_date)) {
        s->last_f5_date = 0;
        fixed++;
    }

    /* sleep log ring indices */
    if (s->night_count > SG_LOG_RETAIN_NIGHTS || s->night_head >= SG_LOG_RETAIN_NIGHTS) {
        clear_nights(s);
        fixed++;
    } else {
        for (i = 0; i < (int)s->night_count; i++) {
            SgNight *n = &s->nights[(s->night_head + i) % SG_LOG_RETAIN_NIGHTS];
            if (n->bed_ms <= 0 || n->bed_ms > SG_TIME_MAX_MS) {
                /* a record without a valid bed time cannot be interpreted: drop the log */
                clear_nights(s);
                fixed++;
                break;
            }
            if (n->wake_ms < 0 || n->wake_ms > SG_TIME_MAX_MS) {
                n->wake_ms = 0;
                fixed++;
            }
            if (n->closed > 1) {
                n->closed = 1;
                fixed++;
            }
        }
    }

    /* F5 reference ring indices and samples */
    if (s->ref_count > SG_REF_SAMPLES_MAX || s->ref_head >= SG_REF_SAMPLES_MAX) {
        clear_refs(s);
        fixed++;
    } else {
        for (i = 0; i < (int)s->ref_count; i++) {
            mod = s->ref_mod[(s->ref_head + i) % SG_REF_SAMPLES_MAX];
            if (mod < 0 || mod > 1439) {
                s->ref_mod[(s->ref_head + i) % SG_REF_SAMPLES_MAX] =
                    (int16_t)(mod < 0 ? 0 : 1439);
                fixed++;
            }
        }
    }

    return fixed;
}
