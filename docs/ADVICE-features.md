# ADVICE-features: Sueño-Guía (SG) feature spec

Scope: personal sideloaded Android 15/16 app, C core, no INTERNET permission. Spec in English, user strings in Spanish (voseo).
All times are epoch milliseconds internally. "T" = next alarm trigger time from AlarmManager.getNextAlarmClock().
Minutes are integers. Constants in section 7 are authoritative; if prose and table differ, the table wins.

## 1. Rationale

- Adults need >= 7 h regularly (AASM/Sleep Research Society consensus, Watson et al. 2015); National Sleep Foundation recommends 7-9 h (Hirshkowitz et al. 2015). Default target 8 h is the middle of that range.
- Chronic 6 h/night for 14 days produced cognitive deficits comparable to 2 nights of total deprivation, while subjects rated themselves only "slightly sleepy" (Van Dongen et al. 2003). The user's current 6 h pattern is not self-detectable.
- Healthy sleep latency is ~10-20 min and nightly awakenings cost another ~15-30 min of time in bed (typical sleep efficiency 90-95% in healthy adults; Ohayon et al. 2017 meta-analysis). So time in bed must exceed target sleep.
- Lead time 9h30 = 8 h target sleep + 20 min latency + ~70 min for evening routine (dinner done, teeth, screens off). It is the time to START getting ready, not lights-out. In-bed time is T - 8h20 with defaults.
- Evening light/screens delay melatonin onset and lengthen latency (Chang et al. 2015, PNAS), which supports a wind-down hint rather than a "go to sleep now" order.
- Weekend catch-up only partly repays debt: in a controlled study, weekend recovery sleep did not prevent metabolic dysregulation or cognitive lapses from weekday restriction, and a Sunday-night circadian shift made Monday worse (Depner et al. 2019, Current Biology).
- Social jetlag (mid-sleep on free days minus work days; Wittmann et al. 2006, Roenneberg et al. 2012) of >= 1 h is associated with worse mood, higher BMI and fatigue. Rule of thumb used here: keep weekend wake time within ~1 h of weekday wake time.
- Consistency of wake time is the strongest lever to anchor the circadian clock; therefore the app is anchored on the ALARM (wake time), not on a fixed bedtime.
- Gentle, non-punitive framing: guilt-based nudges reduce adherence in behaviour-change apps; one calm, actionable sentence per notification.
- The app measures sleep OPPORTUNITY (bedtime tap to alarm), not sleep. It never claims to measure actual sleep.

## 2. Feature specs

Global gate G (applies to F1, F2, F3, F5-alarm): an alarm T is "relevant" only if ALL hold:
1. T != null and T > now.
2. Creator package of AlarmClockInfo.getShowIntent() is in the allow-list (SG_PKG_ALLOW: `com.sec.android.app.clockpackage`, `com.google.android.deskclock`) OR creator package is null/unknown (fail open). Anything else is ignored (calendar/timer/task apps).
3. Local time-of-day of T is within [SG_WAKE_WIN_START_MIN, SG_WAKE_WIN_END_MIN] = 04:00..12:00 (filters naps, afternoon reminders, cooking timers set via alarm clock).
4. T - now <= SG_MAX_HORIZON_MIN (36 h). Farther alarms are stored but scheduled later (re-evaluated at next trigger).
Master toggle `enabled` (default true) disables everything.

### F1 Main reminder
- Fire time: F1_at = T - lead. lead = user setting `lead_min`, default 570 (9h30), range 480..660 (8h..11h), step 15.
- Scheduling: AlarmManager.setExactAndAllowWhileIdle (permission USE_EXACT_ALARM). Fallback if exact not allowed: setAndAllowWhileIdle.
- Fires once per distinct T value. Persist `last_f1_for_T`; do not fire again if equal.
- Grace: if now > F1_at but now - F1_at <= SG_F1_GRACE_MIN (30), fire F1 normally (e.g. after reboot or Doze delay). If later than grace, route to F3.
- Content: alarm time (HH:mm local), suggested in-bed time = T - (target_sleep_min + SG_LATENCY_MIN) formatted HH:mm. If in-bed time is already past, show the F3-style text instead of a past time.
- Actions: "Me voy a dormir" (F4), "En 15 min" (snooze: re-post F1 at now+15 min; max SG_SNOOZE_MAX = 2 per alarm; hide the action after that).
- Auto-removed when: user taps "Me voy a dormir", T passes, alarm cancelled/changed (replaced), or after SG_F1_TIMEOUT_MIN = 180 min.
- Persist: lead_min, target_sleep_min, last_f1_for_T, snooze_count.

### F2 Wind-down pre-reminder
- Default OFF (the 9h30 lead already includes ~70 min of routine; a second ping the same evening risks becoming noise). Toggle `winddown_on`.
- When on: fires at F1_at - SG_WINDDOWN_MIN (default 30; range 15..60 step 15). Once per T. Never fires if now is already past F1_at.
- Content: one calm sentence ("bajá luces y pantallas"), no actions. Importance DEFAULT, auto-cancel, timeout 60 min, cancelled when F1 fires.
- Persist: winddown_on, winddown_min, last_f2_for_T.

### F3 Late-alarm notice
- Trigger: the observed T changed (new alarm set, time edited, or app launched/boot sees a relevant T for which F1_at passed more than grace ago) AND G passes.
- Debounce: wait SG_DEBOUNCE_S = 90 s after the LAST change broadcast and evaluate only the final T. Several edits in a row therefore produce at most one evaluation.
- Possible sleep: avail = T - now - SG_LATENCY_MIN; rounded DOWN to a multiple of 15 min; shown as "H h MM min".
- Notify only if avail >= SG_LATE_MIN_SLEEP_MIN (240 = 4 h). Under 4 h nothing is shown (not actionable, would only add stress).
- Cooldown: max one F3 (or F1) per SG_COOLDOWN_MIN = 180 min. Edits inside the cooldown are silent; the app screen still shows the new numbers.
- Never F3 when avail >= target_sleep_min AND F1_at is still in the future (that is plain F1 territory).
- Tone when avail < target: state the fact, then "dormí lo que puedas", no blame. Tapping body opens app. Action "Me voy a dormir" included.
- Persist: last_notified_ms, last_seen_T.

### F4 Sleep log and weekly debt
- Action "Me voy a dormir" (F1/F3/F2-no) is handled by a BroadcastReceiver, no UI opens. It stores a record {bed_ms = now, wake_ms = current relevant T}. Valid only if now is in [T - lead - 120 min, T - 60 min]; otherwise ignored (toast-free, silent).
- Record is "open" until wake_ms passes. While open, if the observed T changes, wake_ms follows the new T (if new T is null, wake_ms = null and the night shows "—"). Record is closed at the first evaluation after wake_ms.
- Night is attributed to the local calendar date of wake_ms.
- opportunity = wake_ms - bed_ms. est_sleep = max(0, opportunity - SG_LATENCY_MIN).
- Duplicate taps within SG_TAP_DEDUP_MIN = 30 min update bed_ms only if later; no second record.
- Weekly debt (screen): over the last 7 calendar nights ending today, only LOGGED nights count: debt_min = max(0, sum(target_sleep_min - est_sleep_min)). Surplus nights offset deficits (weekend recovery is credited, but see rationale). Show "de N noches registradas"; unlogged nights are shown as "sin registro" and not counted. If N = 0 show empty state. If est_sleep >= target the night is green-neutral (no colour shaming; use a tick).
- Target: `target_sleep_min` default 480, range 420..540 step 15, editable in settings.
- Screen shows: next alarm, next reminder time, last 7 nights (date, bed HH:mm, wake HH:mm, est. sleep), weekly debt, F5 status line if applicable.
- Retention: keep 90 nights, delete older. Ring buffer; no export.
- Persist: records[90] {bed_ms, wake_ms}, target_sleep_min.

### F5 Social-jetlag guard
- Reference: `weekday_ref_min` = median local minute-of-day of the last up to SG_REF_SAMPLES_MAX = 10 samples, requires >= SG_REF_SAMPLES_MIN = 3. A sample = relevant T captured when F1 fires (or on bed tap) for a wake date Monday-Friday. Samples cleared on timezone change.
- Alarm case: when F1 fires for a T whose local wake date is Saturday or Sunday, compute delta = minute-of-day(T) - weekday_ref_min. If delta > SG_JETLAG_TRIGGER_MIN (90), post one extra low-importance hint 1 min after F1: suggested latest wake = weekday_ref_min + SG_JETLAG_SUGGEST_MIN (60), rounded to 15 min. Once per T. Sunday-night alarms (Monday wake) never trigger it.
- No-alarm case (nothing to read): if on Friday or Saturday at SG_NOALARM_HOUR_MIN = 21:30 local there is no relevant T before 12:00 next day, and the ref is valid, and toggle `jetlag_noalarm_on` (default true) is set: post one low-importance hint with the suggested latest wake time (same formula). Once per date.
- If ref invalid (< 3 samples): F5 stays silent; screen shows "Todavía no hay datos de tu horario de semana".
- Toggle `jetlag_on` default true. Persist: ref samples[10], last_f5_date.

### F6 (extra, optional, cheap): "Hoy dormís X" daily number
Skipped. Instead of a sixth feature, F1 body already carries the in-bed time; do not add more.

## 3. Edge cases

| Case | Behavior |
|---|---|
| Alarm cancelled / none (T null) | Cancel pending F1/F2/F5 alarms and remove their posted notifications. Screen: "No tenés alarma puesta". No-alarm F5 hint may still run (Fri/Sat). Open sleep record gets wake_ms = null. |
| Alarm changed before F1 fired | Cancel old schedule, compute new F1_at, reschedule. If F1_at is future: silent. |
| Alarm changed after F1 fired | Remove F1 notification, reset snooze_count. If new F1_at is future: reschedule silently and clear last_f1_for_T. Else go through F3 rules (debounce, cooldown, 4 h threshold). |
| Alarm moved later after bed tap | Open record follows the new T. No notification. |
| Multiple alarms | Only getNextAlarmClock() counts. When the next one rings/dismisses, re-read and schedule the following one (see re-read triggers below). |
| Non-Clock app alarm is the next one | Ignored by G.2. Known limitation: it can mask a later Clock alarm, so no reminder until it passes. Setting `only_clock` (default true); unchecking accepts any package. |
| Alarm in the afternoon/evening | Ignored by G.3. |
| Re-read triggers | Manifest + dynamic receiver for ACTION_NEXT_ALARM_CLOCK_CHANGED; ACTION_BOOT_COMPLETED; ACTION_MY_PACKAGE_REPLACED; ACTION_TIME_CHANGED; ACTION_TIMEZONE_CHANGED; app opened; plus safety net: an inexact repeating re-check every SG_RECHECK_MIN = 60 min (do not rely only on the broadcast being delivered). |
| DST change | Lead is an absolute duration, T is epoch, so math is correct. Display in local time at post time. In-bed time near the change is shown as computed (no special text). Weekday_ref uses local minute-of-day, unaffected. |
| Timezone change | Re-read T, recompute F1_at, clear F5 samples, keep sleep log (epochs). Reschedule. |
| Manual clock change | Same as timezone: full re-evaluation; F1 uses last_f1_for_T so no duplicates. |
| Phone rebooted | Scheduled alarms are lost: on BOOT_COMPLETED re-read T and reschedule. If F1_at passed within grace: fire F1. Beyond grace: F3 rules. |
| App force-stopped | Nothing runs until user opens the app; on open do a full re-evaluation. Screen shows nothing special. |
| Do Not Disturb / Samsung Modes (Sueño, Dormir) | Do not request policy access and do not bypass DND. Notification is still posted and visible in the shade even if silenced. No retry, no repeat. Screen shows "Próximo recordatorio" so nothing is lost. |
| POST_NOTIFICATIONS denied (or channel disabled) | App keeps working (log, screen). Show a persistent in-app banner with a button to system settings. Ask for the permission once on first launch after an explanation screen; never ask again automatically. |
| Exact alarm not permitted | Fall back to setAndAllowWhileIdle (may drift up to ~10-15 min). No banner. |
| Battery optimization | Not requested. Mention nothing unless the user reports missing reminders. |
| Alarm very close (< 4 h of possible sleep) | No F3. F1 not applicable. |
| Bed tap outside window | Ignored silently; notification is removed. |
| Clock alarm snoozed/ringing | Samsung may report T = snooze time. Only re-read on broadcast; G.3 window and open-record logic keep it harmless since F1 uses last_f1_for_T per T value and cooldown applies. |

## 4. Notification design

| Id (channel) | Name (es) | Importance | Used by | Notes |
|---|---|---|---|---|
| sg_reminder | Recordatorio para dormir | IMPORTANCE_HIGH (heads-up, sound, vibration) | F1 | One per alarm. Not ongoing. |
| sg_winddown | Preparación para dormir | IMPORTANCE_DEFAULT (sound, no heads-up) | F2 | Off by default. |
| sg_late | Alarma cercana | IMPORTANCE_DEFAULT | F3 | Separate so it can be muted alone. |
| sg_hints | Sugerencias | IMPORTANCE_LOW (no sound, no heads-up) | F5 | Never interrupts. |

Notification ids: F1 = 1001, F2 = 1002, F3 = 1003, F5 = 1005 (re-posting replaces the previous one).
- All: setAutoCancel(true), not ongoing, category CATEGORY_REMINDER, visibility PUBLIC (no sensitive data), small icon moon (monochrome), content intent opens main screen.
- Timeouts: F1 180 min, F2 60 min, F3 120 min, F5 180 min (setTimeoutAfter).
- Actions: F1 and F3: "Me voy a dormir" (broadcast, no activity) ; F1 also "En 15 min". F2, F5: none.
- Tone rules: second person voseo, max one sentence of fact + one of suggestion, no exclamation marks, no words "tarde", "mal", "deberías". BigText expanded form allowed, collapsed body <= 90 chars.
- No repeating notifications, no ongoing notification, no foreground service, no sound override (channel defaults).

## 5. String table (res/values/strings.xml, es)

| Resource id | Text (es-AR) |
|---|---|
| str_app_name | Sueño-Guía |
| str_ch_reminder_name | Recordatorio para dormir |
| str_ch_reminder_desc | Te avisa con tiempo antes de tu alarma. |
| str_ch_winddown_name | Preparación para dormir |
| str_ch_winddown_desc | Un aviso suave antes del recordatorio principal. |
| str_ch_late_name | Alarma cercana |
| str_ch_late_desc | Te cuenta cuánto podés dormir si la alarma quedó cerca. |
| str_ch_hints_name | Sugerencias |
| str_ch_hints_desc | Consejos tranquilos, sin sonido. |
| str_f1_title | Es hora de ir preparándote para dormir |
| str_f1_body | Alarma a las %1$s. Para dormir %2$s, acostate a las %3$s. |
| str_f1_body_late | Alarma a las %1$s. Si te acostás ahora, podés dormir %2$s. |
| str_f1_action_sleep | Me voy a dormir |
| str_f1_action_snooze | En 15 min |
| str_f2_title | En un rato, a dormir |
| str_f2_body | Bajá las luces y dejá las pantallas. Tu alarma suena a las %1$s. |
| str_f3_title | Tu alarma quedó cerca |
| str_f3_body | Alarma a las %1$s. Si te acostás ya, podés dormir unas %2$s. |
| str_f3_body_ok | Alarma a las %1$s. Todavía llegás a dormir %2$s. Andá a la cama. |
| str_f5_title_alarm | Sugerencia para el finde |
| str_f5_body_alarm | Tu alarma es a las %1$s, %2$s más tarde que en la semana. Probá no pasar de las %3$s. |
| str_f5_title_noalarm | Mañana sin alarma |
| str_f5_body_noalarm | Levantarte antes de las %1$s te ayuda a no desfasar tu reloj. |
| str_fmt_duration | %1$d h %2$02d min |
| str_fmt_duration_min | %1$d min |
| str_fmt_time | %1$02d:%2$02d |
| str_fmt_date_short | %1$s %2$d/%3$d |
| str_screen_title | Tu sueño |
| str_screen_next_alarm | Próxima alarma: %1$s |
| str_screen_next_alarm_none | No tenés alarma puesta |
| str_screen_next_alarm_ignored | Hay una alarma de otra app (%1$s). No la tengo en cuenta. |
| str_screen_next_reminder | Próximo recordatorio: %1$s |
| str_screen_next_reminder_none | Sin recordatorio programado |
| str_screen_last_nights | Últimas 7 noches |
| str_screen_night_row | %1$s: te acostaste %2$s, alarma %3$s, ~%4$s de sueño |
| str_screen_night_empty | sin registro |
| str_screen_night_nowake | sin alarma, no se puede calcular |
| str_screen_debt_title | Deuda de sueño de la semana |
| str_screen_debt_value | %1$s (sobre %2$d noches registradas) |
| str_screen_debt_zero | Sin deuda esta semana |
| str_screen_debt_empty | Todavía no hay noches registradas. Tocá "Me voy a dormir" cuando te acuestes. |
| str_screen_disclaimer | Esto mide el tiempo que tuviste para dormir, no el sueño real. |
| str_screen_jetlag_ok | Tu horario del finde viene parecido al de la semana. |
| str_screen_jetlag_nodata | Todavía no hay datos de tu horario de semana. |
| str_settings_title | Ajustes |
| str_settings_enabled | Activar recordatorios |
| str_settings_lead | Avisarme antes de la alarma |
| str_settings_lead_summary | %1$s antes (rango 8 h a 11 h) |
| str_settings_target | Horas de sueño objetivo |
| str_settings_winddown | Aviso previo de preparación |
| str_settings_winddown_min | Anticipación del aviso previo |
| str_settings_late | Avisar si la alarma quedó cerca |
| str_settings_jetlag | Sugerencias para el finde |
| str_settings_jetlag_noalarm | Sugerir hora de levantarme si no hay alarma |
| str_settings_only_clock | Solo alarmas del Reloj |
| str_settings_clear_log | Borrar registro de noches |
| str_settings_clear_confirm | ¿Borrar todo el registro? No se puede deshacer. |
| str_btn_confirm | Borrar |
| str_btn_cancel | Cancelar |
| str_perm_notif_title | Permitir notificaciones |
| str_perm_notif_body | Sin permiso no puedo avisarte antes de tu alarma. Todo se queda en tu teléfono. |
| str_perm_notif_allow | Permitir |
| str_perm_notif_later | Ahora no |
| str_perm_notif_banner | Las notificaciones están desactivadas. No vas a recibir avisos. |
| str_perm_notif_open_settings | Abrir ajustes |
| str_error_generic | Algo salió mal. Probá abrir la app de nuevo. |
| str_error_alarm_read | No pude leer tu próxima alarma. |

## 6. Non-goals

- No internet, no accounts, no cloud, no analytics, no export/share of the log.
- No measuring of real sleep (no sensors, no microphone, no accelerometer, no Health Connect).
- No reading of alarm lists or editing/creating alarms (only getNextAlarmClock()).
- No fixed-bedtime scheduler, no recurring alarms of our own, no wake-up alarm, no sound-based sleep aids.
- No bypassing Do Not Disturb, no policy-access request, no ignore-battery-optimization prompt, no foreground service, no ongoing notification.
- No streaks, scores, badges, red warnings, or shame wording.
- No shift-work/night-worker mode, no multi-profile, no widgets, no wearables.
- No medical claims or advice about sleep disorders; no caffeine/nap coaching.
- No per-weekday custom targets; one target, one lead.
- No notification repeats/escalation if ignored.

## 7. Config constants

| Constant | Type | Default | Unit | Range / notes |
|---|---|---|---|---|
| SG_LEAD_DEFAULT_MIN | int | 570 | min | 480..660, step 15 (SG_LEAD_MIN_MIN=480, SG_LEAD_MAX_MIN=660, SG_LEAD_STEP_MIN=15) |
| SG_TARGET_SLEEP_DEFAULT_MIN | int | 480 | min | 420..540, step 15 |
| SG_LATENCY_MIN | int | 20 | min | fixed, assumed sleep latency |
| SG_WINDDOWN_DEFAULT_ON | bool | 0 | flag | off by default |
| SG_WINDDOWN_MIN | int | 30 | min | 15..60, step 15 |
| SG_F1_GRACE_MIN | int | 30 | min | fixed |
| SG_F1_TIMEOUT_MIN | int | 180 | min | fixed |
| SG_SNOOZE_MIN | int | 15 | min | fixed |
| SG_SNOOZE_MAX | int | 2 | count | per alarm |
| SG_WAKE_WIN_START_MIN | int | 240 | min of day | 04:00 |
| SG_WAKE_WIN_END_MIN | int | 720 | min of day | 12:00 |
| SG_MAX_HORIZON_MIN | int | 2160 | min | 36 h |
| SG_DEBOUNCE_S | int | 90 | s | fixed |
| SG_LATE_MIN_SLEEP_MIN | int | 240 | min | min possible sleep to notify F3 |
| SG_COOLDOWN_MIN | int | 180 | min | between F1/F3 notifications |
| SG_ROUND_STEP_MIN | int | 15 | min | rounding of shown durations/times |
| SG_BED_WINDOW_BEFORE_MIN | int | 120 | min | bed tap valid from T - lead - 120 |
| SG_BED_WINDOW_AFTER_MIN | int | 60 | min | bed tap valid until T - 60 |
| SG_TAP_DEDUP_MIN | int | 30 | min | fixed |
| SG_LOG_RETAIN_NIGHTS | int | 90 | nights | fixed |
| SG_DEBT_WINDOW_NIGHTS | int | 7 | nights | fixed |
| SG_REF_SAMPLES_MAX | int | 10 | count | fixed |
| SG_REF_SAMPLES_MIN | int | 3 | count | below this F5 is silent |
| SG_JETLAG_TRIGGER_MIN | int | 90 | min | delta above weekday ref to trigger |
| SG_JETLAG_SUGGEST_MIN | int | 60 | min | suggested latest wake = ref + 60 |
| SG_NOALARM_HOUR_MIN | int | 1290 | min of day | 21:30 Fri/Sat |
| SG_RECHECK_MIN | int | 60 | min | inexact safety re-read |
| SG_NOTIF_ID_F1 / F2 / F3 / F5 | int | 1001/1002/1003/1005 | id | fixed |
| SG_F2_TIMEOUT_MIN / SG_F3_TIMEOUT_MIN / SG_F5_TIMEOUT_MIN | int | 60 / 120 / 180 | min | fixed |
| SG_ONLY_CLOCK_DEFAULT | bool | 1 | flag | package allow-list filter |
| SG_PKG_ALLOW | string[] | com.sec.android.app.clockpackage, com.google.android.deskclock | pkg | null creator = accept |
