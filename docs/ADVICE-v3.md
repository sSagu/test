# ADVICE-v3: "Anotar una noche a mano" (manual night entry)

Authority for v3. Extends ADVICE-features.md (§9), ADVICE-architecture.md, ADVICE-v2.md (S1–S9, M1–M6 and every
v2 rule still apply). Design: docs/design/README.md "v3 addition", main-screen.dc.html (link at the end of the
week card), manual-night-sheet.dc.html (bottom sheet). User request: "añadime que dormí 6 horas hoy" — right after
install the owner enters **"hoy, sáb 10/10 / noche del vie al sáb: 6 h 00 min"** and must see it on the chart and
in the debt at once.
Contract files (advisor-written; operators do NOT edit): `cpp/include/*.h`, `sg_jni.c`, `Native.java`, `Makefile`
(JAVA_SRC += NightSheet), `res/values/*`, `res/color/*`, `res/drawable/*`, this file.

## 1. Decisions (and why)

- **D1 A manual night is an ordinary record**, no new field: wake = local `D` at **07:00** (`SG_MANUAL_WAKE_MOD`),
  bed = wake − (N + `SG_LATENCY_MIN`) min, `closed = 1`. est = opportunity − 20 = **N exactly** (ms arithmetic,
  so a DST night is still N). 07:00 is inside the 04–12 wake window, far from midnight and from every DST
  transition (02:00/03:00), so the record is attributed to `D` by the existing wake-date rule. Store **v1
  unchanged** (2304 bytes): nothing new is persisted, so no migration.
- **D2 Picker sends the date, not an index.** Java echoes `UI_NIGHTS[i].NIGHT_DATE` (a C number, never computed in
  Java). C accepts only the 7 dates today−6..today (today = local date of `now`): a sheet left open across midnight
  saves into the night the user saw or is refused; never into a shifted slot.
- **D3 One new JNI entry** `nativeLogNight(now, next, creator, exact, notif, date, minutes) → long[]`, registered
  in `sg_jni.c` through `mutate()` (lock, work copy, adopt on SG_OK, persist iff the image changed). `nativeSet`
  is not reused: it would need two values and a fake key, and it runs a full sync, which v3 must not.
- **D4 C clamps, does not round.** minutes → [60, 840]; the 15-min step is only the stepper increment. A night
  already logged as 7 h 55 min (default of the sheet) saved untouched stays 7 h 55 min.
- **D5 Range 60..840, default 360.** 1 h–14 h like the approved mockup; 840 + 20 ≤ `SG_MAX_OPPORTUNITY_MIN` (960),
  so the 16 h clamp never cuts a manual night. Default for an unlogged night = 6 h 00 min: the approved mockup's
  value and the owner's reported usual night (features §1), so the requested entry is one tap on Guardar. A
  logged night (status 1) defaults to its current est (clamped) so the sheet shows what will be replaced.
- **D6 Ring order is kept**: replace in place, or insert chronologically, and an OPEN record always stays
  newest (`sg_log_open_bed_for`, `follow_alarm`, `retap` only look at the newest record).
- **D7 Side-effect free.** No F5 sample, no `last_*`/debounce/snooze/flag change, no sync, empty command list.
- **D8 Framework Dialog** (`R.style.SgSheetDialog`) anchored at the bottom with a custom layout; dim + slide-in
  from the framework; back, outside tap and Cancelar dismiss. No AndroidX, no new Activity (lint-apk still sees 2).

## 2. Behaviour (`sg_core_log_night` → `sg_log_put_manual`; headers are normative)

| Situation for wake date D | Result |
|---|---|
| no record attributed to D | insert {bed, wake, closed 1} at its chronological slot, before an open newest record; return 1 |
| closed record(s) for D (button or manual) | the NEWEST record for D is overwritten in place (bed, wake, closed 1); return 2 |
| record for D without wake (status 2, alarm removed) | overwritten: it now has a wake, status 1 |
| OPEN record waking on D (alarm today, e.g. 07:30, user saves "hoy" at 06:45) | overwritten and closed (the user chose to replace that night). `last_f1_for_T == T` was set by the tap, so sync plans nothing; BED_STATE becomes CLOSED (or AVAILABLE inside the window: a new tap appends a fresh record, newest wins) |
| OPEN record waking on another date (tonight's tap for tomorrow) | untouched, stays newest: BED_STATE LOGGED, rule L, follow and Actualizar unchanged |
| ring full (90) | oldest record evicted, then insert |
| date not in today−6..today, now invalid, NULL | `SG_E_ARG`, state and file untouched, no commands |
| minutes < 60 / > 840 | clamped to 60 / 840 |
| master toggle off | works (the log is data); no commands |

Window semantics (checked): `sg_log_week` column 6 = local date of now; a night is attributed to its wake date.
"hoy, sáb 10/10" = night Fri→Sat, wake Sat 07:00 → column 6, visible immediately (also when saved before 07:00).
The owner's entry at Sat 2026-10-10 ~07:00, target 8 h, empty log: est 360, BARS[6] 600, LABEL_NIGHT 6,
DEBT_MIN 120 ("2 h 00 min", "sobre 1 noche registrada"), DEBT_STATE SOME.

## 3. UI model v3 (appended after the 85 v2 words; `SG_UI_LEN = 103`)

| idx | SG_UI_* / Native.UI_* | meaning |
|---|---|---|
| 85 | MANUAL_OK | 1 iff today's date is known (`week.night[6].date != 0`): link visible, sheet usable |
| 86–88 | MANUAL_MIN, MANUAL_MAX, MANUAL_STEP | 60, 840, 15 (stepper bounds/increment) |
| 89–95 | MANUAL_DEFAULT[i] | status 1: clamp(est, 60, 840); else 360 |
| 96–102 | MANUAL_PREV_WDAY[i] | weekday (0 dom..6 sáb) of night[i].date − 1 day; −1 if unknown |

Per-night date/weekday: v1 `UI_NIGHTS + i*UI_NIGHT_WORDS + NIGHT_DATE / NIGHT_WDAY`; i = 6 (`Native.NIGHT_TODAY`)
is today. Picker text: i == 6 → `str_manual_date_today(fmtDate)` else `fmtDate`; second line
`str_manual_night_span(wday[PREV_WDAY[i]], wday[NIGHT_WDAY])` (gone if PREV_WDAY < 0). Value `fmtDur(minutes)`.

## 4. JNI (contract, done)

`sg_jni.c`: `do_log_night` → `sg_core_log_night(s, o, date, minutes, out)` via `mutate()`; table entry
`{"nativeLogNight", "(JJIIIII)[J"}`; `SG_JNI_METHOD_COUNT 8`; `to_java_array` buffer is now
max(SG_MAX_CMDS·SG_CMD_WORDS, SG_UI_LEN) (103 > 96 would otherwise return null). `Native.java` mirrors it.

## 5. View-ID tables

**activity_main.xml** — one addition, last child of `box_week` (after the debt row):

| id | type / style | binds |
|---|---|---|
| btn_manual_night | Button `SgLinkButton`, text @string/str_manual_link | visible iff MANUAL_OK==1; click → open the sheet |

**sheet_manual_night.xml** (new). Root `ScrollView @id/sheet_root` (bg `bg_sheet`, fillViewport, overScrollMode
ifContentScrolls; A3-1: the sheet scrolls in a short window instead of clipping Cancelar/Guardar) holding one
`LinearLayout @id/sheet_content` style `SgSheet` with every row below:

| id | type / style | binds / purpose |
|---|---|---|
| (handle) | View `SgSheetHandle` | decorative |
| txt_sheet_title | TextView `SgText.SheetTitle`, marginTop sg_sheet_gap | str_manual_title |
| txt_sheet_sub | TextView `SgText.Label`, marginTop 4dp | str_manual_sub |
| row_night | LinearLayout `SgSheetRow` | — |
| btn_night_prev | ImageButton `SgSheetChevron`, src ic_chevron_left, cd str_cd_manual_prev | enabled iff i > 0; i−1, minutes = DEFAULT[i] |
| box_night_label | LinearLayout vertical, w 0 weight 1, gravity center, focusable true, accessibilityLiveRegion polite | — |
| txt_night_date / txt_night_span | `SgText.SheetDate` / `SgText.Small` (gravity center_horizontal, marginTop 2dp) | §3 picker text |
| btn_night_next | ImageButton `SgSheetChevron`, src ic_chevron_right, cd str_cd_manual_next | enabled iff i < 6; i+1, minutes = DEFAULT[i] |
| row_minutes | LinearLayout `SgSheetRow`, minHeight sg_row_stepper_min (72dp), paddingStart/End 16dp | — |
| txt_minutes_label | TextView `SgText.SettingTitle`, w 0 weight 1 | str_manual_slept |
| btn_minutes_minus / btn_minutes_plus | Button `SgStepButton`, str_step_minus / str_step_plus, marginStart 12dp on minus | cd str_cd_manual_minus/plus(STEP); enabled iff minutes ∓ STEP within [MIN, MAX]; minutes ∓= STEP |
| txt_minutes_value | TextView `SgText.SheetValue` | fmtDur(minutes) |
| txt_sheet_note | TextView `SgText.Small`, marginTop sg_sheet_gap, lineSpacingMultiplier 1.3 | str_manual_note |
| row_sheet_buttons | LinearLayout horizontal, marginTop sg_sheet_gap | — |
| btn_sheet_cancel / btn_sheet_save | Button `SgSheetButtonCancel` / `SgSheetButtonSave` | str_btn_cancel → dismiss / str_btn_save → save |

## 6. Task cards (parallel, disjoint ownership; nobody edits contract files)

Common: C — no heap/strcpy/sprintf/alloca/VLA/recursion, gcc+NDK clang `-Wall -Wextra -Werror -Wconversion
-Wshadow -Wvla` clean, clang-tidy/cppcheck clean, `dropped == 0`. Java — Java 8 (no `->`/`::`), framework only,
no hard-coded text, no logging, no time/date math.

### CARD core — `app/src/main/cpp/sg_sleeplog.c`, `app/src/main/cpp/sg_core.c`
1. sg_sleeplog.c: `sg_log_attr_date`, `sg_log_put_manual` exactly per sg_sleeplog.h (static `attr_ms` helper;
   insertion = evict-if-full, p from the open-newest rule, walk back while attr_ms(p−1) > wake, shift top-down
   with `idx_of`). `sg_log_week` may call `sg_log_attr_date`; its output must stay byte-identical.
2. sg_core.c: `sg_core_log_night` per sg_core.h (cmd_init first; `loc_of(now)`; loop k = −6..0 for the date;
   clamp; put_manual; nothing else). Never call sync.
3. `sg_core_ui`: fill the v3 fields after `sg_log_week` (§3); `sg_core_ui_flatten`: write 85..102.
Accept: `make test`, `make valgrind`, clang-tidy/cppcheck part of `make lint`.

### CARD tests — `tests/test_sleeplog.c`, `tests/test_core.c`
Update `_Static_assert(SG_UI_LEN == 103)` and the v2 "bars end the model" assert to
`SG_UI_BARS + 7 == SG_UI_MANUAL_OK`. Register every new test. TZ: core tests run under TZ_BA; DST tests call
`sg_tz_set("Europe/Madrid")` (restored by SG_RUN). Every core test: `out.count == 0`, `out.dropped == 0`.
- sleeplog `manual_attr_date`: wake>0 → wake date; wake 0 → date(bed+12 h); NULL → 0.
- `manual_insert`: empty log, put(D_SAT, 360) → 1; wake == sg_at(D_SAT,420), bed == wake − 380 min, closed 1,
  est 360; week(D_SAT): night[6] status 1, est 360, debt 120, logged 1; ref_count unchanged.
- `manual_args`: NULL, 59, 841, bad date 20261332 → −1, encoded image unchanged (sg_store_encode + memcmp).
- `manual_replace`: closed tap record for D_FRI → put(D_FRI, 420) → 2, count same, est 420; two records for
  D_FRI → only the newer is overwritten; a status-2 record for D → status 1.
- `manual_order`: records for Thu and Sat, put Fri → sg_log_at(1) is the Fri record.
- `manual_keeps_open_newest`: open record waking D_SUN (newest) + put(D_SAT) → open record still last,
  open_bed_for(T) unchanged, follow_alarm(T+30 min) moves only the open record.
- `manual_replace_open_same_date`: open record waking D_SAT 07:30, put(D_SAT, 360) → 2, closed 1, est 360,
  open_bed_for == 0.
- `manual_ring_full`: 90 records (newest closed, then variant with newest open): put → count 90, oldest
  evicted, new record found, open record still newest.
- `manual_dst`: Madrid 20261025 (fall back) and 20270328 (spring forward): est == 360, attr date == D.
- core **`v3_user_scenario`** (the owner's exact case): defaults, now = sg_at(D_SAT, 420) Sat 2026-10-10 07:00,
  no alarm. UI before: night[6] date D_SAT wday 6, MANUAL_OK 1, MANUAL_DEFAULT[6] 360, PREV_WDAY[6] 5,
  MIN/MAX/STEP 60/840/15. `sg_core_log_night(D_SAT, 360)` → SG_OK, 0 commands. UI after: night[6] status 1
  est 360, BARS[6] 600, LABEL_NIGHT 6, DEBT_MIN 120, DEBT_STATE SOME, LOGGED_COUNT 1; flattened 85..102 match.
  Same result when saved at 06:40 (wake still ahead).
- `v3_args`: NULL s/o/out; date tomorrow, today−7, 0 → SG_E_ARG + image unchanged; minutes 0 → 60, 5000 → 840,
  455 → 455 (no rounding).
- `v3_no_side_effects`: with refs, an alarm and F1 posted: ref_count, last_seen_T, last_f1/f2/f5_for_T,
  last_notified_ms, debounce, snooze_count, flags unchanged; works with ENABLED off.
- `v3_replace`: tap-logged night (SLEEP Thu 23:00, T Fri 07:00, synced Fri 08:00) → log_night(D_FRI, 300)
  → est 300, count same; again 315 → 315.
- `v3_open_record_safe`: SLEEP Thu 23:00 for T = Fri 07:00; log_night(D_THU, 360) and (D_THU−1, 420) →
  BED_STATE LOGGED, APP_OPEN sync → no SCHEDULE F1/F2, no DEBOUNCE; BROADCAST T+30 min → open record wake ==
  T2, manual records unchanged.
- `v3_replace_open_today`: SLEEP Thu 23:00 for T = Fri 07:30; at Fri 06:45 log_night(D_FRI, 360) → night[6] est
  360; APP_OPEN sync → 0 NOTIFY, no SCHEDULE F1/F2/DEBOUNCE; BED_STATE CLOSED.
- `v3_ui_defaults`: est 475 → 475; est 0 → 60; est 900 → 840; status 2 → 360; PREV_WDAY[i] == (wday+6)%7.
- `v3_midnight`: date D_FRI saved at Sat 00:01 → SG_OK (index 5); D_FRI−6 at Sat 00:01 → SG_E_ARG.
- `v3_dst_night`: Madrid, now 20261025 08:00, log_night(20261025, 360) → night[6] est 360, debt 120.
Accept: `make test` (3 TZs) and `make valgrind` green.

### CARD ui — `res/layout/activity_main.xml`, `res/layout/sheet_manual_night.xml` (new), `MainActivity.java`, `NightSheet.java` (new)
- activity_main: add `btn_manual_night` (§5) as the last child of `box_week`. Nothing else moves.
- sheet_manual_night.xml exactly per §5 (all text @string, styles/drawables from the contract files).
- `final class NightSheet` (package ar.sg): `interface Saved { void onSaved(); }`;
  `static Dialog show(Activity a, long[] ui, Saved cb)`: `new Dialog(a, R.style.SgSheetDialog)`, setContentView,
  `setTitle(R.string.str_manual_title)` (TalkBack names the window), cancelable + canceledOnTouchOutside; window
  `setLayout(MATCH_PARENT, WRAP_CONTENT)`, `setGravity(Gravity.BOTTOM)`, `setDecorFitsSystemWindows(false)`,
  `attrs.setFitInsetsTypes(0)`; `sheet_root` (the ScrollView) insets listener: paddingBottom =
  navigationBars inset (the sheet's own sg_sheet_pad_bottom is inside sheet_content). Draft state: i = `Native.NIGHT_TODAY`, minutes = DEFAULT[i]; render() per §3/§5. Save:
  `Sg.observe` → `Native.nativeLogNight(o…, (int) date_i, minutes)` → `Sg.run` if non-null → dismiss →
  `cb.onSaved()`. Own static copies of fmtDur / fmtDate / wdayShort (ADVICE-v2 §3 rules, Context param).
- MainActivity: bind `btn_manual_night` visibility; click → observe + `nativeUiModel`; if length ≥ UI_LEN and
  MANUAL_OK == 1 → `sheet = NightSheet.show(this, ui, cb)` with cb → `bind(Sg.observe(this))`; dismiss `sheet`
  in onDestroy. Everything else in MainActivity unchanged.
Accept: `make apk`, `make lint` (lint-apk unchanged: 2 activities), `grep -nE -- '->|::' app/src/main/java/ar/sg/*.java`
empty, `grep -n 'android:text="[^@]' app/src/main/res/layout/*.xml` empty.
