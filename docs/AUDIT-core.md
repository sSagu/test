# AUDIT-core: pure-C core (sg_core, sg_time, sg_state, sg_sleeplog, sg_store)

Auditor: core audit advisor, 2026-10-09. Scope: `app/src/main/cpp/{sg_core,sg_time,sg_state,sg_sleeplog,sg_store}.c`.

**Method.** I read all 5 files against `ADVICE-features.md` §2/§3/§7, `ADVICE-architecture.md` §7/§8-C3 and the headers. Then I ran the journeys through a simulated Java executor: pending alarms by id, posted notifications, alarms fired in time order. Build: gcc 13 `-std=c11 -fsanitize=address,undefined -fno-sanitize-recover=all`, with TZ=America/Argentina/Buenos_Aires and TZ=Europe/Madrid. I also ran 20 000 random CRC-valid state images through decode, sync, every alarm_fired id, both actions, set, ui and flatten, under ASan/UBSan and under valgrind. Every fix below was applied to a scratch copy and re-run against all journeys and the existing `tests/` suite. The two expected test changes are listed with the fixes.

## Findings

| id | sev | file:line | problem | reproducer | observed vs expected |
|---|---|---|---|---|---|
| K1 | **BLOCKER** | sg_core.c:361-374 | In the T-unchanged branch of `sg_core_sync`, F1 is only rescheduled when `now < f1`. No sync ever recovers an F1 alarm that was lost while T stayed the same. That covers reboot (alarms are wiped), force-stop, an alarm lost on update, and a lead change that moves F1_at into the past. The grace path and the F3 path exist only in the T-changed branch. | Journey c. T=Tue 07:00, sync Mon 15:00 (F1 set for 21:30). Phone off 21:25, BOOT sync at 21:45, or at 22:20. See Appendix A. | Observed: boot 21:45 gives no F1 and no debounce. Boot 22:20 gives nothing. RECHECK repeats the same nothing all night, so **no reminder that night**. Expected (§3 "Phone rebooted"): 21:45 gives an F1 now; 22:20 gives the F3 rules (F3_OK, 8h15). |
| K2 | HIGH | sg_core.c:576-597 (`fire_f1` guard :407) | When an F1, F2 or F5_HINT alarm fires with `T != last_seen_T` (the user edited the alarm and the broadcast was lost or late), the reminder is dropped silently and nothing is re-planned. Recovery waits for the next inexact RECHECK. | Sync 21:00 with T=07:00. User edits to 07:20 at 21:20 with no broadcast. The F1 alarm fires at 21:30. | Observed: nothing at 21:30. F1 posts at 22:00 (next RECHECK). If RECHECK slips past 22:20 (Doze/inexact), it becomes F3 or nothing. Expected: re-plan at 21:30, giving F1 at 21:50 (verified with the fix). |
| K3 | MEDIUM | sg_core.c:375-390 | An alarm more than 36 h away (HORIZON) cancels the F1 alarm. The only path to ever scheduling it is the inexact hourly RECHECK. The typical case: Friday morning, next alarm Mon 07:00 (72 h). Sunday's F1 depends entirely on RECHECK running between Sat 19:00 and Sun 21:30. In the restricted standby bucket, with Samsung "sleeping apps", or with alarms batched away, Monday's reminder never exists. | Fri 07:30 sync with T=Mon 07:00. RECHECK starved, no other trigger. | Observed: F1=0 all weekend. Expected: F1 Sun 21:30 (verified with the fix). |
| K4 | MEDIUM | sg_core.c:171 | The debounce is armed with `SG_SCHED_EXACT` (plain setExact, which is deferred in Doze). After K1, a debounce can start from a night-time RECHECK or BOOT, and each later sync re-issues it at now+90 s, so it can be starved until a maintenance window. The architecture's reason for EXACT (the 9-min throttle) applies only while idle. EXACT_IDLE behaves the same when awake. | K1 fix plus a RECHECK in Doze. | Expected: the debounce fires about 90 s after it is armed. |
| K5 | MEDIUM | sg_sleeplog.c:71-82 | `sg_log_follow_alarm` moves an open record's wake to *any* new T. The user dismisses the upcoming 07:00 alarm at 06:50, so T jumps to tomorrow 07:00 and last night's record gets wake = tomorrow (32 h, clamped to a 16 h estimate) and is attributed to tomorrow. Tomorrow night's tap creates a new record for the same date, which "newest wins" overwrites. | Journey: tap Sun 22:40 (T Mon 07:00), sync Mon 06:50 with T=Tue 07:00, tap Mon 22:40. | Observed in the Tue week view: Mon "sin registro", logged=1. Expected: Mon 480 and Tue 480, logged=2 (verified with the fix). |
| K6 | LOW | sg_core.c:336-347, 375-390 | On a T change or alarm removal, a posted F3 (and, on removal, a posted F5 hint) is not cancelled. It keeps showing the old alarm time and "you can sleep X" for up to 120/180 min. A new F3 is usually cooldown-silenced, so the only visible notification is the stale one. | F3 posted for 07:00, user changes the alarm to 06:00. | Expected: remove stale F3/F5 (spec §3 "remove their posted notifications"). |
| K7 | LOW | sg_core.c:431, 636-638 | F5 weekday samples are added on every F1 post, including snooze re-posts (up to 3 per T), plus once more on the bed tap. With 10 slots the reference covers about 2.5 nights instead of 10. | Journey a with 2 snoozes and a tap: 4 samples for one T. | The fix-K1 snippet samples only on the first F1 post per T. The tap duplicate remains (median-neutral for a stable routine). |
| K8 | LOW | sg_core.c:648 | SNOOZE is accepted when no F1 is visible. A double-delivered tap burns the second snooze, so the re-post loses its "En 15 min" button. | Two SNOOZE actions in a row. | Fixed by the `F1_POSTED` check in fix-K1. |
| K9 | LOW | design | A reboot during a 15-min snooze window loses the snoozed re-post (no due time is persisted). The user already saw F1 once. | Snooze 21:31, reboot 21:40. | Accept; documented. |
| K10 | LOW | sg_sleeplog.c:84-101 | A record whose wake became 0 (alarm removed; also an early Friday dismiss, because Monday is HORIZON) never closes, and the night shows "—". This matches the spec but loses Friday-night data. | Fri 06:50 dismiss. | Optional fix below. |

Clean areas, verified rather than assumed:
- **Store (journey j).** A truncated file gives E_CORRUPT and a `.bad` quarantine. A symlink gives E_IO and defaults. 20k hostile CRC-valid images (counts up to 65535, heads out of range, negative or huge times, snooze=255, lead=0/65535) were all sanitized, with no ASan/UBSan finding, valgrind clean and 0 leaks. Save leaves no `.tmp`, mode 0600, and the reload returns OK.
- **Command list.** Peak 11 of 12 under fuzz, `dropped` = 0. With every fix applied, the worst case is still 11.
- **Arithmetic.** No signed overflow found: all ms math is int64 on sanitized inputs, and `diff_min` saturates. Ring indices are always taken modulo N after sanitize.
- **Journeys a, b, d, e, f, g, h, i:**
  - a. F1 21:30 NORMAL (bed 22:40), tap at 22:40, HOUSEKEEP 07:01 closes the record, week view Mon = 480 min.
  - b. 3 edits within 40 s at 23:30 give one debounce at 23:32:10, cooldown-silent after the 21:30 F1. A fresh install gives exactly one F3.
  - d. Cancel after F1: notification and F1/F2/F5H alarms cancelled; RECHECK and F5_NOALARM stay.
  - e. Saturday 10:30 with ref 07:00: F1 Sat 01:00, F5 hint 01:01 (delta 210, suggest 08:00), once.
  - f. Friday and Saturday 21:30 no-alarm hint once each; RECHECK all weekend posts nothing.
  - g. RECHECK all night: no duplicate NOTIFY, no past-time reschedule loop.
  - h. Madrid DST 2026-10-25: F1 at 22:30 local (absolute 9h30 lead, per §3 "DST change"); week view 25th = 490 min (8h30 real − 20).
  - i. Clamp of INT32_MIN/MAX/487/59 gives 480/660/480/45. Disabling cancels all 7 alarms and 4 notification kinds.

## Deviation verdicts

- **C3-1** (F5 samples only on F1 fire and new bed-tap record, not on every sync): **ACCEPT.** This is what features §F5 says. Sampling on every sync would oversample hourly. Add the first-post guard from fix-K1 (K7).
- **C3-3** (SNOOZE resets `last_f1_for_T` to 0): **REJECT.**
  - On its own it does not duplicate: journey verified F1 + 2 snoozes = 3 posts, no F3. A T change during the snooze legitimately re-plans.
  - It is incompatible with the K1 recovery. A T-unchanged sync during the snooze window would see "F1 not done, F1_at passed". Within grace it re-posts F1 immediately. Beyond grace it routes to F3 and marks T handled, which then blocks the snoozed re-post.
  - It also re-samples F5 (K7).
  - Replace with: keep `last_f1_for_T = T`, and let `fire_f1` re-post only when `snooze_count > 0 && !F1_POSTED` (fix-K1, part 3).
- **C3-4** (debounce re-issued on a same-T sync only while one is pending): **ACCEPT, extended.** Fix-K1 keeps it: re-issue while pending with the same T, and also start one when none is pending and F1 is past grace. It needs K4 (EXACT_IDLE) so that a sync re-issuing it cannot starve it in Doze.
- **C1 civil-day `date_add`/`wday` instead of mktime at noon:** **ACCEPT.** It is exact and TZ-free, so no DST gap can shift a date. Verified across Madrid 2026-10-25. `tzset()` per call: **ACCEPT.** Bionic reads `persist.sys.timezone` in tzset, so a long-lived process sees TZ changes. The cost is negligible.
- **C1 sleeplog `bed_tap`/`follow`/`close` only on the newest open record:** **ACCEPT** for tap and follow (the spec's "open record" is the newest). `close_if_due` actually scans every record, which is correct. `follow` needs the K5 bound.

## Fixes (apply literally; all validated together, `-Wconversion -Wshadow` clean)

### Fix-K1 (BLOCKER), 4 parts, all in `app/src/main/cpp/sg_core.c`

**Part 1.** In `sg_core_sync`, replace:
```c
            /* T unchanged: idempotent re-plan of F1/F2. */
            if (now < f1 && s->last_f1_for_T != T) {
                sg_cmd_schedule(out, SG_ALARM_F1, f1, SG_SCHED_EXACT_IDLE);
            }
            sched_f2_or_cancel(s, f1, now, T, out);

            /* A late change is still waiting for its debounce: push it out again. */
            if (s->last_f1_for_T != T && now > f1 + min_ms(SG_F1_GRACE_MIN) &&
                has_flag(s, SG_FLAG_LATE_ON) && s->debounce_due_ms != 0 &&
                s->debounce_T == T) {
                start_debounce(s, now, T, out);
            }
```
with:
```c
            /* T unchanged: idempotent re-plan of F1/F2. Also recovers an F1 alarm that
             * was lost (reboot, force-stop, update) or moved into the past (lead change):
             * within grace -> F1 now; later -> one F3 evaluation (fire_debounce then marks
             * T handled, so RECHECK cannot repeat it). */
            if (s->last_f1_for_T != T) {
                if (now < f1) {
                    sg_cmd_schedule(out, SG_ALARM_F1, f1, SG_SCHED_EXACT_IDLE);
                } else if (now <= f1 + min_ms(SG_F1_GRACE_MIN)) {
                    sg_cmd_schedule(out, SG_ALARM_F1, now, SG_SCHED_EXACT_IDLE);
                } else if (has_flag(s, SG_FLAG_LATE_ON) &&
                           (s->debounce_due_ms == 0 || s->debounce_T == T)) {
                    start_debounce(s, now, T, out);
                }
            }
            sched_f2_or_cancel(s, f1, now, T, out);
```

**Part 2.** At the end of `fire_debounce`, replace:
```c
        s->last_notified_ms = now;
    }
    clear_debounce(s);
}
```
with:
```c
        s->last_notified_ms = now;
    }
    if (now > f1 + min_ms(SG_F1_GRACE_MIN)) {
        s->last_f1_for_T = T;   /* F1 time is over for this T: evaluate F3 once only */
    }
    clear_debounce(s);
}
```
Without this, a T-unchanged RECHECK would start a new F3 evaluation every hour, and an F3 would repost every 180 min overnight.

**Part 3** (replaces C3-3). In `fire_f1`, replace:
```c
    int64_t bed_ms;
    Loc bed;

    if (rel != SG_REL_OK || T != s->last_seen_T || s->last_f1_for_T == T) {
        return;
    }
```
with:
```c
    int64_t bed_ms;
    Loc bed;
    int first;

    if (rel != SG_REL_OK || T != s->last_seen_T) {
        return;
    }
    /* Once per T; only a snoozed F1 (snooze_count > 0, notification withdrawn) re-posts. */
    if (s->last_f1_for_T == T &&
        (s->snooze_count == 0 || has_flag(s, SG_FLAG_F1_POSTED))) {
        return;
    }
    first = (s->last_f1_for_T != T);
```

In the same function, replace:
```c
    sample_if_weekday(s, T);

    {
```
with:
```c
    if (first) {
        sample_if_weekday(s, T);
    }

    {
```

In `sg_core_action`, replace:
```c
        if (s->snooze_count < SG_SNOOZE_MAX) {
            s->snooze_count = (uint8_t)(s->snooze_count + 1u);
            sg_cmd_cancel_notify(out, SG_NK_F1);
            put_flag(s, SG_FLAG_F1_POSTED, 0);
            /* Allow the snoozed F1 to post again for the same T. */
            s->last_f1_for_T = 0;
```
with:
```c
        if (s->snooze_count < SG_SNOOZE_MAX && has_flag(s, SG_FLAG_F1_POSTED)) {
            s->snooze_count = (uint8_t)(s->snooze_count + 1u);
            sg_cmd_cancel_notify(out, SG_NK_F1);
            put_flag(s, SG_FLAG_F1_POSTED, 0);
            /* last_f1_for_T stays T: fire_f1 lets a snoozed F1 re-post. */
```

**Part 4 (K4).** In `start_debounce`, replace:
```c
    sg_cmd_schedule(out, SG_ALARM_DEBOUNCE, due, SG_SCHED_EXACT);
```
with:
```c
    sg_cmd_schedule(out, SG_ALARM_DEBOUNCE, due, SG_SCHED_EXACT_IDLE);
```
Test update: `tests/test_core.c:206` must expect `SG_SCHED_EXACT_IDLE`.

### Fix-K2 (HIGH), `sg_core.c` in `sg_core_alarm_fired`

Replace:
```c
    rel = sg_core_relevant(s, o);
    T = o->next_alarm_ms;

    switch (alarm_id) {
```
with:
```c
    rel = sg_core_relevant(s, o);
    T = o->next_alarm_ms;

    /* The alarm clock changed and no broadcast reached us: re-plan now instead of
     * silently dropping the reminder. */
    if ((alarm_id == SG_ALARM_F1 || alarm_id == SG_ALARM_F2 || alarm_id == SG_ALARM_F5_HINT) &&
        (rel == SG_REL_OK ? T != s->last_seen_T : s->last_seen_T != 0)) {
        return sg_core_sync(s, o, SG_REASON_RECHECK, out);
    }

    switch (alarm_id) {
```
There is no loop: the sync sets `last_seen_T`, so the next firing takes the normal path.

### Fix-K3 (MEDIUM), `sg_core.c` in `sg_core_sync`, "No relevant alarm" branch

Fix-K2 is required. Replace:
```c
        put_flag(s, SG_FLAG_F1_POSTED, 0);
        sg_cmd_cancel_alarm(out, SG_ALARM_F1);
        sg_cmd_cancel_alarm(out, SG_ALARM_F2);
        sg_cmd_cancel_alarm(out, SG_ALARM_F5_HINT);
        clear_debounce(s);
    }
```
with:
```c
        put_flag(s, SG_FLAG_F1_POSTED, 0);
        if (rel == SG_REL_HORIZON) {
            /* Beyond 36 h: keep an exact wake-up at its F1 time so a starved RECHECK
             * cannot lose the reminder; alarm_fired(F1) then re-plans through sync. */
            sg_cmd_schedule(out, SG_ALARM_F1, f1_of(s, T), SG_SCHED_EXACT_IDLE);
        } else {
            sg_cmd_cancel_alarm(out, SG_ALARM_F1);
        }
        sg_cmd_cancel_alarm(out, SG_ALARM_F2);
        sg_cmd_cancel_alarm(out, SG_ALARM_F5_HINT);
        clear_debounce(s);
    }
```
HORIZON implies T ≤ SG_TIME_MAX_MS, because WINDOW is checked first. Test update: `tests/test_core.c:109` ("40 h away: stored but not scheduled") must now expect `SCHEDULE(F1, T - lead)`.

### Fix-K5 (MEDIUM), `app/src/main/cpp/sg_sleeplog.c` in `sg_log_follow_alarm`

Replace:
```c
    if (newest < 0 || s->nights[newest].closed != 0) {
        return;
    }
    s->nights[newest].wake_ms = (T_ms < 0) ? 0 : T_ms;
```
with:
```c
    if (newest < 0 || s->nights[newest].closed != 0) {
        return;
    }
    /* A T more than SG_MAX_OPPORTUNITY_MIN after bed belongs to a later night (alarm
     * dismissed before it rang): keep this night's wake time. */
    if (T_ms > 0 && T_ms - s->nights[newest].bed_ms > SG_MIN_TO_MS(SG_MAX_OPPORTUNITY_MIN)) {
        return;
    }
    s->nights[newest].wake_ms = (T_ms < 0) ? 0 : T_ms;
```

### Fix-K6 (LOW), `sg_core.c` in `sg_core_sync`

In the T-changed block, replace:
```c
            sg_cmd_cancel_notify(out, SG_NK_F2);
            sg_cmd_cancel_alarm(out, SG_ALARM_F1);
```
with:
```c
            sg_cmd_cancel_notify(out, SG_NK_F2);
            sg_cmd_cancel_notify(out, SG_NK_F3);
            sg_cmd_cancel_alarm(out, SG_ALARM_F1);
```

In the "No relevant alarm" branch (8-space indent), replace:
```c
        sg_cmd_cancel_notify(out, SG_NK_F2);
        put_flag(s, SG_FLAG_F1_POSTED, 0);
```
with:
```c
        sg_cmd_cancel_notify(out, SG_NK_F2);
        sg_cmd_cancel_notify(out, SG_NK_F3);
        sg_cmd_cancel_notify(out, SG_NK_F5);
        put_flag(s, SG_FLAG_F1_POSTED, 0);
```
The worst case stays at 11 of 12 commands.

### Fix-K10 (LOW, optional), `sg_sleeplog.c` in `sg_log_close_if_due`

Replace:
```c
        if (n->closed == 0 && n->wake_ms > 0 && now_ms >= n->wake_ms) {
```
with:
```c
        if (n->closed == 0 &&
            ((n->wake_ms > 0 && now_ms >= n->wake_ms) ||
             (n->wake_ms == 0 && now_ms - n->bed_ms > SG_MIN_TO_MS(SG_MAX_OPPORTUNITY_MIN)))) {
```

### After applying

`make test && make valgrind` should pass with only the two test-expectation edits (lines 109 and 206). With all fixes, the journeys gave:

| journey | result |
|---|---|
| J1 | Mon 480 |
| J2 | F1=1, F3=0 |
| J3 boot 21:45 | F1 21:45 |
| J4 boot 22:20 | F3_OK 22:21:30 |
| J5 | no duplicate |
| J7 | snooze 3 posts |
| J8 lead→660 at 21:00 | 1 F3 |
| J9 | 2 nights |
| J14 | 1 F3 overnight |
| J16 | F1 21:50 |
| J17 | F1 Sun 21:30 |

Fuzz: 0 drops, ASan clean.

## Appendix A: K1 reproducer (26 lines)

Build with `gcc -std=c11 -Iapp/src/main/cpp/include k1.c app/src/main/cpp/sg_{core,time,state,sleeplog,store}.c` and run with `TZ=America/Argentina/Buenos_Aires`. It currently prints `F1=NO DEBOUNCE=NO` for both boots. With the fix it prints `F1=yes` at 21:45 and `DEBOUNCE=yes` at 22:20.
```c
#include <stdio.h>
#include "sg_core.h"
#include "sg_time.h"
static int64_t sched_at(const SgCmdList *l, int id) {
    for (int i = 0; i < l->count; i++)
        if (l->cmd[i].type == SG_CMD_SCHEDULE && l->cmd[i].a[0] == id) return l->cmd[i].a[1];
    return 0; }
int main(void) {
    SgState s; SgCmdList l; sg_state_defaults(&s);
    int64_t T = sg_time_from_local(20261013, 7 * 60);            /* Tue 07:00 */
    SgObs o = {sg_time_from_local(20261012, 15 * 60), T, SG_CREATOR_ALLOWED, 1, 1};
    sg_core_sync(&s, &o, SG_REASON_BROADCAST, &l);              /* F1 scheduled 21:30 */
    for (int m = 21 * 60 + 45; m <= 22 * 60 + 20; m += 35) {     /* reboot lost the F1 alarm */
        SgState b = s;
        o.now_ms = sg_time_from_local(20261012, m);
        sg_core_sync(&b, &o, SG_REASON_BOOT, &l);
        printf("boot %02d:%02d -> F1=%s DEBOUNCE=%s\n", m / 60, m % 60,
               sched_at(&l, SG_ALARM_F1) ? "yes" : "NO", sched_at(&l, SG_ALARM_DEBOUNCE) ? "yes" : "NO");
    }
    return 0;
}
```
The full journey simulator (an executor with alarms and notifications, 17 journeys) and the store fuzzer are in `/tmp/audit2/drv.c` and `/tmp/audit2/fuzz.c`. They are not in the repo.
