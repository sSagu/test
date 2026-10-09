/* sg_state.h - the single in-memory state struct (everything that is persisted).
 * Owned by whoever holds the SgState instance (the JNI glue holds exactly one static
 * instance guarded by a mutex; tests hold one on the stack). No pointers, no heap.
 * Layout here is NOT the file layout: sg_store.h serialises field by field.
 */
#ifndef SG_STATE_H
#define SG_STATE_H

#include <stdint.h>
#include "sg_config.h"

/* flags bits (persisted as one u8) */
#define SG_FLAG_ENABLED          (1u << 0)
#define SG_FLAG_WINDDOWN_ON      (1u << 1)
#define SG_FLAG_LATE_ON          (1u << 2)
#define SG_FLAG_JETLAG_ON        (1u << 3)
#define SG_FLAG_JETLAG_NOALARM   (1u << 4)
#define SG_FLAG_ONLY_CLOCK       (1u << 5)
#define SG_FLAG_NOTIF_PROMPTED   (1u << 6)   /* POST_NOTIFICATIONS asked once */
#define SG_FLAG_F1_POSTED        (1u << 7)   /* F1 notification currently believed visible */

/* One logged night. bed_ms > 0 always. wake_ms == 0 means unknown (alarm removed).
 * closed == 1 once an evaluation ran after wake_ms. */
typedef struct {
    int64_t bed_ms;
    int64_t wake_ms;
    uint8_t closed;
} SgNight;

typedef struct {
    /* settings */
    uint8_t  flags;              /* SG_FLAG_* */
    uint8_t  snooze_count;       /* for current last_f1_for_T */
    uint16_t lead_min;           /* SG_LEAD_MIN_MIN..SG_LEAD_MAX_MIN */
    uint16_t target_sleep_min;
    uint16_t winddown_min;

    /* scheduler memory (epoch ms, 0 = none) */
    int64_t last_seen_T;         /* last relevant T observed */
    int64_t last_f1_for_T;       /* T for which F1 was posted */
    int64_t last_f2_for_T;
    int64_t last_f5_for_T;
    int64_t last_notified_ms;    /* last F1 or F3 post time (cooldown) */
    int64_t debounce_due_ms;     /* F3 evaluation pending at this time, 0 = none */
    int64_t debounce_T;          /* T captured when debounce started */
    int32_t last_f5_date;        /* yyyymmdd of last no-alarm F5 hint, 0 = none */

    /* sleep log ring buffer: nights[(head + i) % N], i < count, oldest first */
    uint16_t night_count;
    uint16_t night_head;
    SgNight  nights[SG_LOG_RETAIN_NIGHTS];

    /* F5 weekday wake reference samples (minute-of-day), ring buffer like nights */
    uint8_t  ref_count;
    uint8_t  ref_head;
    int16_t  ref_mod[SG_REF_SAMPLES_MAX];
} SgState;

/* Reset to factory defaults (SG_*_DEFAULT_*), empty log. Never fails. */
void sg_state_defaults(SgState *s);

/* Clamp every setting into its legal range and step; fix impossible ring indices
 * (count > N or head >= N -> empty). Returns number of fields corrected (0 = clean).
 * Called by sg_store_load after a successful parse and by sg_core before use. */
int sg_state_sanitize(SgState *s);

#endif /* SG_STATE_H */
