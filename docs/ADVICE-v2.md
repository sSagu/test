# ADVICE-v2: in-app bedtime, reboot re-post, redesigned screens

Authority for v2. Extends ADVICE-features.md / ADVICE-architecture.md (all S1–S9, M1–M6 still apply).
Design source: docs/design/README.md + main-screen.dc.html + settings-screen.dc.html (approved).
Contract files (advisor-written, operators do NOT edit): `cpp/include/*.h`, `Native.java`,
`AndroidManifest.xml`, `Makefile`, `tools/lint-apk.sh`, `res/values/*`, `res/color/*`, `res/drawable/*`.

## 1. Decisions (and why)

- **D1 Bedtime control is a C state machine** (`SG_UI_BED_*`). The in-app pill and "Actualizar" call the
  EXISTING `Native.nativeAction(…, ACTION_SLEEP)`: same window, same log code as the notification button,
  so request (1) needs no new JNI entry and no new Java decision.
- **D2 "Logged means handled".** A valid tap marks F1/F2 done for T (`last_f1_for_T = last_f2_for_T = T`),
  cancels the F1 alarm (pending F1 *or* snooze), F2, debounce and the F1/F2/F3 notifications. Sync applies
  the same rule whenever the open record belongs to T (also after the alarm moved: features §3 "No
  notification"). Without it, a tap before 21:30 would still be followed by the 21:30 reminder.
- **D3 Re-tap = update, never a second record.** `sg_log_bed_retap`: if the newest record is open and
  `wake_ms == T`, `bed_ms = max(bed_ms, now)` at any distance (v1 30-min dedup still applies to other
  records). No duplicate nights in the ring, no duplicate F5 weekday sample.
- **D4 Reboot re-post** only on `BOOT`/`PKG_REPLACED`, only if F1 was posted for this T and not answered by
  a tap, inside the bed window and inside F1's own 180-min lifetime, posted **silently** with the remaining
  timeout. Flag rides in NOTIFY a5 (`SG_NOTIFY_SILENT = 1<<8`, Java → `setSilent(true)`); no new command.
  RECHECK/APP_OPEN/BROADCAST never re-post → no loops; same notification id → no duplicates.
- **D5 Settings = separate `SettingsActivity`** (exported=false, no intent-filter, explicit Intent from the
  gear). Back stack, predictive-back animation (targetSdk 36 + `enableOnBackInvokedCallback="true"`) and
  insets come free per Activity; a 2-container single Activity would need an `OnBackInvokedCallback` and
  view-state juggling. Files stay disjoint. `lint-apk.sh` now also asserts: exactly 2 activities, only
  MainActivity exported, SettingsActivity explicitly exported=false and without intent-filter.
- **D6 Persistence v1 unchanged** (`sg_store.h`). Everything new is derived from existing fields
  (`last_f1_for_T`, `last_notified_ms`, `SG_FLAG_F1_POSTED`, `snooze_count`, the night ring).
- **D7 Faithful to the mockup:** the v1 per-night text rows and the social-jetlag line leave the screen
  (F5 hints still arrive as notifications; model fields 7–9 stay for compatibility). Kept because they are
  functional: notification banner, error line, and the wind-down anticipation stepper (shown only when
  "Aviso previo" is on, so the default screen equals the mockup). Mockup sub-line "Si te acostás más
  tarde…" became "…después…" (tone rule: no "tarde"). Lead stepper a11y labels corrected (− = later).
- **D8 Theme parent → `android:Theme.Material.NoActionBar`** so vendor skins cannot restyle Switch/Button.

## 2. Bedtime control behaviour (C: `sg_core_ui` + `sg_core_action`)

`f1 = T − lead`, `lo = f1 − 120 min`, `hi = T − 60 min` (unchanged F4 window). `logged(T)` =
`sg_log_open_bed_for(s,T) > 0`. Not enabled, or `alarm_rel != OK` → `SG_BED_HIDDEN` (pill not shown).

| bed_state | condition (first match) | bed_mod | Screen (mockup state) | Tap |
|---|---|---|---|---|
| LOGGED 3 | logged(T) | mod(bed_ms) | card "Anotado: te acostaste a las %s"; can_update → sub "Si te acostás después, actualizalo." + **Actualizar**; else sub "Que descanses." | Actualizar → SLEEP → bed_ms = max(bed, now) |
| BEFORE 1 | now < lo | mod(lo) | disabled pill, "Disponible desde las %s" (antes) | none (disabled) |
| AVAILABLE 2 | lo ≤ now ≤ hi | mod(hi) | accent pill + moon, "Podés anotarlo hasta las %s" (disponible) | SLEEP |
| CLOSED 4 | now > hi | mod(hi) | disabled pill, "El registro de esta noche cerró a las %s" | none |
| HIDDEN 0 | off / no relevant alarm | −1 | nothing (sinalarma: message card instead) | — |

`bed_can_update = (state == LOGGED && lo ≤ now ≤ hi)`.
SLEEP action (both entry points): enabled && rel OK && lo ≤ now ≤ hi → `r = sg_log_bed_retap`; r==1 →
weekday sample; r≠0 → housekeeping alarm; then always: handled marks (D2), cancel alarms F1, F2,
DEBOUNCE; cancel notifications F1, F2, F3; clear F1_POSTED. If F1 had not fired for T and weekend drift
applies (and JETLAG_ON, last_f5_for_T != T), arm SG_ALARM_F5_HINT at now+1 min (R2-C2: the weekend hint
survives an early tap). Otherwise v1 behaviour (cancel F1+F3
notifications, clear F1_POSTED, no record). Disabled → no-op (v1).

Edge cases:
- Alarm changed after logging: record follows (v1 `sg_log_follow_alarm`, ≤16 h rule); new T → still LOGGED,
  F1/F2 not scheduled (D2). New T irrelevant → record wake 0, HIDDEN; back to a relevant T → follows again.
- Window edges are inclusive at both ends (`lo` and `hi` accepted, ±1 min rejected) — same as v1 tests.
- Repeated taps: first appends, later taps for the same T only move bed_ms forward; Actualizar disappears
  after hi. Notification still visible when tapping in-app → cancelled by the SLEEP commands.
- Snooze pending → `CANCEL_ALARM F1` removes it; a stale F1 alarm would no-op anyway (`last_f1_for_T == T`).
- Master toggle off → hero DISABLED, pill hidden, action no-op. No alarm / other app / outside 04–12 /
  >36 h → message card, pill hidden. First week without data → 7 dashes, no label, debt EMPTY.

## 3. UI model v2 (appended after the 66 v1 words; `SG_UI_LEN = 85`)

| idx | SG_UI_* / Native.UI_* | meaning |
|---|---|---|
| 66 | HERO | SG_HERO_*: DISABLED if !enabled (first), else by rel: NONE→NO_ALARM 0, OK→ALARM 1, OTHER_APP 2, WINDOW→OUT_OF_WINDOW 3, HORIZON→FAR 4. If rel≠NONE but local time of T fails → NO_ALARM. |
| 67 | ALARM_WDAY | 0=Sun..6 of T's local date (rel≠NONE), else −1. Date digits: v1 ALARM_DATE, time: v1 ALARM_MOD. |
| 68 | REMIND_STATE | only for HERO_ALARM: UPCOMING 1 (`last_f1_for_T≠T && now ≤ f1+30`), SENT 2 (`last_f1_for_T==T && f1−120min ≤ last_notified_ms ≤ now && last_notified_ms < T`), NONE 3 otherwise; HIDDEN 0 if not hero ALARM |
| 69 | REMIND_MOD | UPCOMING: mod(f1); SENT: mod(last_notified_ms); else −1 |
| 70–72 | BED_STATE, BED_MOD, BED_CAN_UPDATE | §2 |
| 73 | DEBT_STATE | EMPTY 0 (logged_count 0), ZERO 1 (debt 0), SOME 2 |
| 74 | NOTIF_BANNER | enabled && !obs.notif_allowed |
| 75 | ASK_NOTIF | !obs.notif_allowed && !SG_FLAG_NOTIF_PROMPTED |
| 76 | TARGET_PERMILLE | target_sleep_min·1000/600 (480 → 800) |
| 77 | LABEL_NIGHT | largest i (0..6) with night status 1, else −1 (only that bar gets a value label) |
| 78–84 | BARS[i] | status 1: clamp(est,0,600)·1000/600 (int); else −1 (dash, never a 0 bar) |

Moon row: v1 BED_SUGGEST_MOD (≥0 only for enabled+OK) + `settings[TARGET]`. Debt: v1 DEBT_MIN, LOGGED_COUNT.
Per-night words (v1): date, wday, status (0 empty, 1 logged, 2 no wake), bed, wake, est.
**Java rendering rules (allowed formatting only):** `fmtMod(m)` = str_fmt_time(m/60, m%60);
`fmtDur(m)` = m<60 ? str_fmt_duration_min : str_fmt_duration; `fmtHours(m)` = m%60==0 ? str_fmt_duration_hours(m/60)
: fmtDur(m); `fmtShort(m)` = str_fmt_duration_short(m/60, m%60) ("7 h 55"); `fmtDate(yyyymmdd, wday)` =
str_fmt_date_short(str_wday_short[wday], dd, mm). Pixel mapping: `px = permille · plotPx / 1000`,
plotPx = `sg_chart_plot_h`; a bar is at least `sg_bar_min_h`.

## 4. Reboot / update re-post (C: `sg_core_sync`, header comment R)

On `SG_REASON_BOOT` or `SG_REASON_PKG_REPLACED`, T relevant and == last_seen_T, `last_f1_for_T == T`,
(`F1_POSTED` || `snooze_count > 0`), !logged(T), lo ≤ now ≤ hi, `0 ≤ now − last_notified_ms < 180 min`:

- **F1 visible** (`F1_POSTED`) → one `NOTIFY F1` (variant/args exactly as fire_f1 at now), a5 = SLEEP |
  (SNOOZE if count<2) | SILENT, a6 = 180 − elapsed_min (≥1); set F1_POSTED.
- **Snooze pending** (`!F1_POSTED`, `snooze_count > 0`; C2 fix) → no NOTIFY. `SCHEDULE F1` at `now + 15 min`
  (SG_SNOOZE_MIN, EXACT_IDLE): the reboot lost the snooze alarm, an update replaces it (same request code).
  fire_f1 then posts an audible snoozed F1 with the SNOOZE action. The reminder is never silent.

Nothing else changes (cooldown, samples, last_notified_ms). Command budget unchanged (one command either way).

**Owed re-post (C3 fix, `sg_state.h` field `boot_unseen`, not persisted).** BOOT/PKG_REPLACED with no next
alarm (`getNextAlarmClock()` null because the clock app has not re-registered yet) and `last_seen_T != 0`
keeps T's state (no cancels, no `last_seen_T = 0`) and sets `boot_unseen`. The next relevant sync with T
unchanged runs rule R with reason BOOT, so the broadcast that follows re-posts the unanswered F1 (or re-arms a
pending snooze). Any OK sync clears the flag.

While the flag is set, every sync that has no relevant alarm (APP_OPEN, RECHECK, SETTING, a BROADCAST with an
empty list, ...) keeps T's state and the flag, as long as `now < last_seen_T` (V2-R1). The app can be opened after
unlocking, before the clock app re-registers its alarm, so an APP_OPEN with an empty list must not drop the owed
re-post. Once T has passed, nothing is owed: such a sync takes the normal "no relevant alarm" branch (F1/F2/F5
cancelled, `last_seen_T = 0`, flag cleared). The flag only lives in memory; a process death drops it (accepted). The JNI glue
(`sg_jni.c` mutate) adopts the work state on every SG_OK call and writes the file only when the encoded
image changed, so an in-memory-only flag such as `boot_unseen` is never lost (R2-C1).

## 5. View-ID tables (ids are the contract between layouts and Java)

**activity_main.xml** (owner ui-main). Root `ScrollView @id/root` (bg sg_ground, fillViewport, clipToPadding
false; Java pads it with systemBars|displayCutout insets) → `LinearLayout style SgScreenColumn`:

| id | type / style | binds / purpose |
|---|---|---|
| txt_screen_title | TextView SgText.Title (w 0, weight 1) in SgTopBar | @string/str_screen_title |
| btn_settings | ImageButton SgIconButton, src ic_gear, cd str_cd_settings, no margin (D2: −12dp offset let the hit area leave the top bar) | opens SettingsActivity |
| txt_error | TextView SgText, gone | visible iff model null/short |
| box_notif_banner | LinearLayout SgCard | visible iff NOTIF_BANNER==1 |
| txt_notif_banner / btn_notif_settings | TextView SgText / Button SgButtonOutline (marginTop 12dp) | str_perm_notif_banner / str_perm_notif_open_settings → app notification settings |
| box_hero_alarm | LinearLayout SgCard | visible iff HERO==ALARM |
| txt_alarm_day | TextView SgText.Label | str_hero_alarm_day(fmtDate(ALARM_DATE, ALARM_WDAY)) |
| txt_alarm_time | TextView SgText.AlarmTime (marginTop 2dp) | fmtMod(ALARM_MOD) |
| (divider) | View SgDivider, marginTop 16dp | — |
| row_remind | LinearLayout SgHeroRow: ImageView SgHeroRowIcon ic_bell + | visible iff REMIND_STATE≠0 |
| txt_remind_label | TextView SgText.Row (w 0, weight 1) | 1 str_remind_upcoming, 2 str_remind_sent, 3 str_remind_none |
| txt_remind_time | TextView SgText.RowValue | fmtMod(REMIND_MOD); gone iff REMIND_MOD<0 |
| row_bed_suggest | LinearLayout SgHeroRow: ImageView SgHeroRowIcon ic_moon_outline + | visible iff BED_SUGGEST_MOD≥0 |
| txt_bed_suggest_label / txt_bed_suggest_time | SgText.Row (w 0, weight 1) / SgText.RowValue | str_bed_suggest_label(fmtHours(settings[TARGET])) / fmtMod(BED_SUGGEST_MOD) |
| box_hero_msg | LinearLayout SgCard (paddingTop/Bottom 24dp) | visible iff HERO≠ALARM |
| txt_hero_msg_title / txt_hero_msg_body | SgText.HeroMsgTitle / SgText.HeroMsgBody (marginTop 8dp) | NO_ALARM: str_hero_none_*; OTHER_APP: other_title(fmtMod(ALARM_MOD)), other_body; OUT_OF_WINDOW: window_title(fmtMod), window_body; FAR: far_title(fmtDate, fmtMod), far_body; DISABLED: off_* |
| box_bed | LinearLayout vertical, marginTop 16dp | visible iff BED_STATE ∈ {1,2,4} |
| btn_bed | LinearLayout SgBedButton | setEnabled(BED_STATE==2); click → SLEEP |
| img_bed_moon / txt_bed_button | ImageView SgBedButtonIcon / TextView SgBedButtonText | moon visible iff BED_STATE==2 / str_bed_button |
| txt_bed_hint | TextView SgText.Hint, marginTop 10dp | 1 str_bed_before, 2 str_bed_available, 4 str_bed_closed (fmtMod(BED_MOD)) |
| box_bed_logged | LinearLayout SgCard horizontal, center_vertical, padding 16/16/16/20 (start 20) | visible iff BED_STATE==3 |
| img_bed_check | ImageView 40dp, bg bg_check_badge, src ic_check, tint sg_on_accent, scaleType center, a11y no | — |
| txt_bed_logged / txt_bed_logged_sub | SgText.Logged / SgText.Small (in vertical LL w 0 weight 1, margins 14dp) | str_bed_logged(fmtMod(BED_MOD)) / CAN_UPDATE ? str_bed_logged_sub : str_bed_logged_done |
| btn_bed_update | Button SgButtonOutline, cd str_cd_bed_update | visible iff CAN_UPDATE==1; click → SLEEP |
| box_week | LinearLayout SgCard, clipChildren false (D4) | always |
| txt_week_title / txt_week_unit | SgText.Section (w 0, weight 1) / SgText.Small, one horizontal row | str_screen_last_nights / str_week_unit |
| chart | FrameLayout h sg_chart_h, marginTop 14dp | — |
| chart_target_line | View match×sg_target_line_h, gravity bottom, bg line_dashed, layerType software | bottomMargin = px(TARGET_PERMILLE)+1dp |
| txt_target_label | TextView SgText.Chart, gravity bottom\|start, paddingEnd 4dp, no background, declared BEFORE chart_cols (drawn under the bars so it never hides a bar or value label; UI-1 / UI2-A1; UI-A3/D3: never meets the newest value label) | str_week_target(fmtHours(settings[TARGET])); bottomMargin = line margin + 4dp |
| chart_baseline | View match×sg_baseline_h, gravity bottom, bg sg_baseline | — |
| chart_cols | LinearLayout horizontal match×match, paddingBottom 1dp | — |
| col_0..col_6 | LinearLayout vertical, w 0 weight 1, h match, gravity bottom\|center_horizontal, focusable, importantForAccessibility yes; marginEnd 2dp except col_6 | cd: status1 str_cd_night_value(fmtDate, fmtDur(est)), 0 str_cd_night_empty, 2 str_cd_night_nowake |
| lbl_0..lbl_6 | TextView SgText.ChartValue, gone, marginBottom 4dp | visible iff i==LABEL_NIGHT: fmtShort(est) |
| bar_0..bar_6 | View w sg_bar_w, h 0, bg bg_bar, gone | BARS[i]≥0 → visible, height max(px, sg_bar_min_h) |
| dash_0..dash_6 | View sg_empty_w×sg_empty_h, bg bg_bar_empty, marginBottom 2dp | visible iff BARS[i]<0 |
| day_0..day_6 | TextView SgText.Chart, w 0 weight 1, center (row marginTop 8dp, marginEnd 2dp except day_6); day_6 textColor sg_text_primary | str_wday_short[night wday] |
| (divider) | View SgDivider, marginTop 14dp | — |
| txt_debt_title / txt_debt_sub | SgText.Row / SgText.Small (vertical LL w 0 weight 1, row marginTop 14dp) | str_screen_debt_title / EMPTY str_debt_empty, else plural str_debt_over_nights(LOGGED_COUNT) |
| txt_debt_value | TextView SgText.Debt, marginStart 12dp | EMPTY gone; ZERO str_debt_none; SOME fmtDur(DEBT_MIN) |
| txt_footnote | TextView SgText.Foot, marginTop 16dp | str_footnote |

**activity_settings.xml** (owner ui-settings). Root `ScrollView @id/settings_root` (same flags) →
`SgScreenColumn` → `SgTopBar` (marginStart sg_back_offset):

| id | type / style | binds / purpose |
|---|---|---|
| btn_back | ImageButton SgIconButton, src ic_back, cd str_cd_back | finish() |
| txt_settings_title | TextView SgText.Title, marginStart 4dp | str_settings_title |
| txt_settings_error | TextView SgText, gone | model null/short |
| card_reminders | LinearLayout SgCard.Settings; first child TextView SgText.CardHeader str_settings_section_reminders | — |
| row_lead | LinearLayout SgStepRow: vertical LL (w 0, weight 1) with txt_lead_label (SgText.SettingTitle, str_settings_lead) + txt_lead_range (SgText.Small, marginTop 2dp, str_settings_lead_range); then btn_lead_minus, txt_lead_value, btn_lead_plus | value fmtDur(settings[LEAD]) |
| btn_lead_minus / btn_lead_plus | Button SgStepButton, text str_step_minus / str_step_plus, cd str_cd_lead_minus / _plus | nativeSet(LEAD, v∓15) |
| txt_lead_value | TextView SgText.StepValue | — |
| (divider) | View SgDivider | after row_lead and after row_target |
| row_target (+ txt_target_label, txt_target_range, btn_target_minus, txt_target_value, btn_target_plus) | same pattern; str_settings_target, str_settings_target_range, cd str_cd_target_* | fmtDur(settings[TARGET]) |
| sw_enabled, sw_late, sw_winddown | Switch SgSwitch, text str_settings_enabled / _late / _winddown | settings[ENABLED/LATE_ON/WINDDOWN_ON] |
| row_winddown_min (+ txt_winddown_label, txt_winddown_range, btn_winddown_minus, txt_winddown_value, btn_winddown_plus) | same stepper pattern; str_settings_winddown_min, str_settings_winddown_range, cd str_cd_winddown_* | visible iff settings[WINDDOWN_ON]==1; fmtDur(settings[WINDDOWN_MIN]) |
| card_weekend | LinearLayout SgCard.Settings; header SgText.CardHeader str_settings_section_weekend | — |
| sw_jetlag, sw_jetlag_noalarm, sw_only_clock | Switch SgSwitch | settings[JETLAG_ON / JETLAG_NOALARM / ONLY_CLOCK] |
| btn_clear_log | Button SgButtonDestructive, str_settings_clear_log | confirm dialog (str_settings_clear_confirm, str_btn_confirm, str_btn_cancel) → nativeSet(CLEAR_LOG, 1) |

## 6. Task cards (parallel; ownership disjoint; nobody edits contract files)

Common: Java-8 syntax (anonymous classes; no `->`/`::`), no AndroidX, no hard-coded user text, no logging.
Every Java entry: `Sg.observe` → ONE `Native.*` call → `Sg.run` → `bind(Sg.observe(...))` via `nativeUiModel`.

### CARD core — `app/src/main/cpp/sg_core.c`, `app/src/main/cpp/sg_sleeplog.c` (sg_jni.c: no change needed)
1. sg_sleeplog.c: implement `sg_log_open_bed_for`, `sg_log_bed_retap` exactly per sg_sleeplog.h.
2. sg_core.c: factor the NOTIFY part of `fire_f1` into `static void post_f1(SgState*, int64_t T, int64_t now,
   int flags, int32_t timeout_min, SgCmdList*)` (variant/args unchanged); `fire_f1` calls it with 0/180.
3. `sg_core_sync`, rel OK branch: keep the T-changed bookkeeping; then rule **L** (sg_core.h): if logged(T)
   → handled marks, clear debounce, emit CANCEL_ALARM F1+F2 when the T-unchanged path is taken and
   `last_f1_for_T != T` (changed path already emitted them), CANCEL_ALARM DEBOUNCE iff a debounce was
   pending; skip all F1/F2/debounce scheduling. Else v1 logic, then rule **R** (§4) in the unchanged path.
4. `sg_core_action(SLEEP)`: §2 / sg_core.h comment. SNOOZE unchanged.
5. `sg_core_ui`: fill every v2 field per §3 / sg_core.h comments (hero, remind via f1 & last_notified_ms,
   bed via lo/hi/open_bed_for, debt_state, notif flags from `o`, permilles, label_night).
   `sg_core_ui_flatten`: write indices 66..84. Keep v1 fields/semantics byte-identical.
6. Command budget ≤ SG_MAX_CMDS on every path (`dropped == 0`); M1/M6; gcc+clang strict-clean.
Accept: `make test` (after tests card), `make valgrind`, `make lint` (clang-tidy/cppcheck part).

### CARD tests — `tests/test_core.c`, `tests/test_sleeplog.c`
Register new functions in `run_core_tests` / `run_sleeplog_tests` like the existing ones. Use T = D_FRI 07:00,
lead 570: f1 Thu 21:30 (1290), lo Thu 19:30 (1170), hi Fri 06:00 (360). Required:
- Update: `_Static_assert(SG_UI_LEN == 85)`; `test_core_command_budget` now expects NO debounce after the
  alarm moves under an open record (rule L), `last_f1_for_T == T_new`, HOUSEKEEP + CANCEL_NOTIFY F1 present.
- sleeplog: `open_bed_for` (empty, open wake==T, closed, wake≠T, T=0, NULL); `bed_retap` (no record →1
  append; open same T 90 min later →2, count same, bed moved; earlier now →0; wake≠T → v1 dedup/append;
  closed → append).
- core `v2_sleep_marks_handled`: tap at 20:30 → CANCEL_ALARM F1/F2/DEBOUNCE + CANCEL_NOTIFY F1/F2/F3,
  last_f1_for_T==T; alarm_fired(F1) at 21:30 → 0 NOTIFY; RECHECK → no SCHEDULE F1, no DEBOUNCE.
- `v2_retap_updates`: taps 23:00 and 00:30 → 1 night, bed 00:30, ref_count unchanged by 2nd tap.
- `v2_snooze_then_sleep`: F1, SNOOZE, SLEEP → CANCEL_ALARM F1; F1 alarm at +15 → 0 NOTIFY.
- `v2_alarm_moved_after_logging`: logged T1, BROADCAST T2=T1+30min → no SCHEDULE F1/F2, wake==T2, UI LOGGED.
- `v2_ui_bed_states`: BEFORE at 19:29 (bed_mod 1170), AVAILABLE at 19:30 and 06:00 (bed_mod 360), CLOSED
  06:01, LOGGED after tap (bed_mod = tap mod, can_update 1) and at 06:01 (can_update 0); HIDDEN when
  disabled / no alarm / REL_WINDOW / REL_HORIZON / REL_OTHER_APP.
- `v2_ui_hero`: each rel → hero, disabled wins, alarm_wday 5 for D_FRI, −1 without alarm.
- `v2_ui_remind`: UPCOMING (mod 1290) at 20:00; SENT after F1 fired at 21:35 (mod 1295); NONE after late
  alarm (sync past f1+30, debounce with avail < 4 h); NONE after tap before F1; HIDDEN without alarm.
- `v2_ui_week`: empty → BARS all −1, LABEL −1, DEBT EMPTY, TARGET_PERMILLE 800; nights with est 450 / ≥600
  → 750 / 1000, LABEL = newest logged index; DEBT ZERO vs SOME; NOTIF_BANNER and ASK_NOTIF truth tables
  (incl. disabled → banner 0); flatten writes 66..84.
- `v2_reboot_repost`: F1 posted 21:30; BOOT 23:00 → exactly 1 NOTIFY F1, a5 has SILENT+SLEEP+SNOOZE,
  a6 == 90, last_notified_ms unchanged, F1_POSTED set; then RECHECK, APP_OPEN, BROADCAST(same T) → 0
  NOTIFY; BOOT at 00:20 → a6 == 10; BOOT at 00:30 → none; PKG_REPLACED = BOOT.
- `v2_reboot_no_repost`: after a tap; T changed across reboot; boot at 06:01 (outside window); F1 never
  posted (v1 grace path, no SILENT bit); snooze_count 2 + F1_POSTED 0 → repost without SNOOZE action.
Every test: `out.dropped == 0`. Accept: `make test` (3 TZs) and `make valgrind` green.

### CARD ui-main — `res/layout/activity_main.xml`, `MainActivity.java`, `Sg.java`
- Layout exactly per §5 table 1 (ids, styles, order = mockup order: top bar, error, banner, hero alarm |
  hero msg, bed control | logged card, week card, footnote). All text via @string; drawables/styles only
  from the contract files. Remove v1 settings views, night rows and jetlag line.
- `MainActivity`: keep edge-to-edge (`setDecorFitsSystemWindows(false)` + insets padding on root), keep
  onResume flow (nativeSync APP_OPEN → run → bind) and onRequestPermissionsResult. `bind()` renders only
  §3/§5 fields with the §3 formatting helpers. Permission dialog: shown iff ASK_NOTIF==1 && !askedThisRun
  (drop the SDK check and nativeGet). Gear → `startActivity(new Intent(this, SettingsActivity.class))`.
  btn_bed / btn_bed_update → `Native.nativeAction(o…, Native.ACTION_SLEEP)` → `Sg.run` → bind. Delete the
  settings code, `Calendar`, jetlag math (all moved to C or to SettingsActivity).
- `Sg.java` notify(): `if ((actions & Native.NOTIFY_SILENT) != 0) b.setSilent(true);` nothing else changes.
Accept: `make apk` once SettingsActivity exists (before: `make build/base.apk` then javac of
`build/gen/ar/sg/R.java app/src/main/java/ar/sg/*.java` plus a throwaway stub SettingsActivity in /tmp);
`grep -nE -- '->|::' app/src/main/java/ar/sg/*.java` empty; `grep -n 'android:text="[^@]'` layout empty.

### CARD ui-settings — `res/layout/activity_settings.xml`, `java/ar/sg/SettingsActivity.java` (new)
- Layout per §5 table 2. `public final class SettingsActivity extends android.app.Activity`.
- onCreate: `getWindow().setDecorFitsSystemWindows(false)`, setContentView, `Sg.init(this)`, insets
  listener on settings_root (systemBars|displayCutout → padding), `setSaveEnabled(false)` on all switches,
  wire listeners. onResume: `Sg.init(this)`; bind. bind = nativeUiModel → switches, stepper values
  (fmtDur), row_winddown_min visibility, under a `binding` guard (like v1 MainActivity).
- Toggle → `nativeSet(key, on?1:0)`; stepper → `nativeSet(key, settings[key] ± 15)` (C clamps); clear →
  AlertDialog → `nativeSet(SET_CLEAR_LOG, 1)`; each followed by `Sg.run` and bind. btn_back → `finish()`.
  Do NOT override onBackPressed (system/predictive back already finishes the Activity).
Accept: `make apk` + `make lint` (lint-apk: 2 activities, SettingsActivity exported=false); Java-8 greps above.
