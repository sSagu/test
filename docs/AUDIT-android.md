# AUDIT-android: Android / Java / JNI side of Sueño-Guía

Auditor: audit advisor (Android scope). Date: 2026-10-09.
Scope: `AndroidManifest.xml`, `app/src/main/java/ar/sg/*.java`, `app/src/main/cpp/sg_jni.c`
(against `cpp/include/*.h`), `app/src/main/res/**`. The pure C core is out of scope.

## How this was verified

- `aapt2 compile` + `aapt2 link` (build-tools 37.0.0, android-37.2 jar) of `res/` + manifest: **OK**.
  `aapt2 dump xmltree` of the linked manifest checked (exported flags, actions, permissions).
- `javac -source 8 -target 8 -Xlint:all -bootclasspath $ANDROID_JAR` of all 6 classes + generated
  `R.java`: **OK**, only warning is the deprecated `setDecorFitsSystemWindows` (A7). This proves every
  `R.id` / `R.string` / `R.array` / `R.drawable` reference exists.
- `sg_jni.c` with NDK clang (`-Wall -Wextra -Werror -Wconversion -Wshadow -Wvla ...`): **OK**.
- Existing `build/sg.apk`: `libsg.so` is `Stored` (uncompressed) and only `JNI_OnLoad`/`JNI_OnUnload`
  are exported, so `extractNativeLibs="false"` and `RegisterNatives` work.
- The proposed fixes for A1, A2, A3 were applied to scratch copies and compiled cleanly with the same flags.

## Verified correct (no action needed)

- `RegisterNatives` descriptors match `Native.java` exactly (7/7): `(Ljava/lang/String;)I`, `(JJIIII)[J` x3,
  `(JJIIIII)[J`, `(I)I`, `(JJIII)[J`. `FindClass` local ref is deleted, exceptions are checked and cleared.
- JNI M2/M3: `GetStringUTFChars` is released on every path; `NewLongArray`/`SetLongArrayRegion` are followed by
  `ExceptionCheck`; the array is built after unlock from a stack copy; the 96-word stack buffer covers both the
  command list (max 96) and the UI model (66). No global refs, no cached `JNIEnv`.
- Action strings: `android.intent.action.TIME_SET` is the real value of `Intent.ACTION_TIME_CHANGED`;
  `TIMEZONE_CHANGED`, `BOOT_COMPLETED`, `MY_PACKAGE_REPLACED`, `android.app.action.NEXT_ALARM_CLOCK_CHANGED` are
  correct. All are sent by system_server, so `exported="false"` receivers still get them.
- PendingIntents: explicit component, `FLAG_IMMUTABLE`, request codes alarms 1-7 / actions 101-102 / content 200,
  identical extras per request code, so `FLAG_UPDATE_CURRENT` never needs to change extras. `FLAG_NO_CREATE` cancel
  lookup matches (the IMMUTABLE flag is part of the key and is the same).
- Exact-alarm fallbacks (`SecurityException` leads to `setAndAllowWhileIdle`/`set`) are present. `setAlarmClock` is
  never called (S9).
- Channels are created before every `notify`. The small icon `ic_moon` is a valid monochrome vector. `setTimeoutAfter`
  uses minutes x 60000L.
- All `getString(id, args)` calls match their placeholders in count and type: f1_body (s,s,s), f1_body_late (s,s),
  f2 (s), f3/f3_ok (s,s), f5_alarm (s,s,s), f5_noalarm (s), fmt_time (d,d), fmt_duration (d,d),
  fmt_duration_min (d), fmt_date_short (s,d,d), next_alarm/ignored/next_reminder (s), night_row (s,s,s,s),
  debt_value (s,d), lead_summary (s).
- UI indices in `Native.java` match `SG_UI_*` and `sg_core_ui_flatten`. Settings are at `[12+key]`, and the
  night words date/status/bed/wake/est are at +0/+2/+3/+4/+5. `str_wday_short` has 7 entries.
- Security: only the launcher activity is exported (S1). Receivers validate action + id range (S2). Backup is off and
  data-extraction rules exclude every domain (S7). There is no `Log` on the Java side and no `__android_log` in cpp (S8).

## Findings

| id | sev | file:line | problem | evidence |
|---|---|---|---|---|
| A1 | MEDIUM | app/src/main/cpp/sg_jni.c:180-188 | On a transient read error, the next save can overwrite the real state file with defaults, wiping up to 90 logged nights and all settings. | `sg_store_load` returns `SG_STORE_E_IO` (open failed with errno other than ENOENT, `fstat` failed, or `read` failed) and leaves `*s` = defaults. `jni_init` still sets `g_loaded = 1` and caches the code. Nearly every later `mutate()` changes state (re-arm, last_seen_T), so `sg_store_save(g_path, defaults)` replaces the user's file. Init is never retried in that process. |
| A2 | MEDIUM | app/src/main/java/ar/sg/MainActivity.java:45-58, 100-105 | Restoring switch state calls `nativeSet` before `nativeInit` and runs the returned commands. | `Switch` (CompoundButton) saves and restores `checked`. `onRestoreInstanceState` runs after `onCreate` (listeners already wired, `binding == false`) and before `onResume` (where `nativeInit` happens). Rotation causes a redundant set. After process death and restore, `nativeSet` runs on the in-memory **defaults**: no persistence, but `Sg.run` executes schedule/cancel commands computed from defaults. Example: a restored `sw_winddown=true` schedules an F2 for a user state that may not want it. |
| A3 | MEDIUM | app/src/main/java/ar/sg/MainActivity.java:50-56 | Edge-to-edge insets drop the left and right insets and ignore the display cutout. | `v.setPadding(0, i.top, 0, i.bottom)` uses `systemBars()` only. `screenOrientation="unspecified"`, so in landscape with 3-button navigation the nav bar (and the punch-hole cutout on the other side) sits over the right/left edge. The +/- buttons and switches at the end of each row end up under the bar. Edge-to-edge cannot be opted out at targetSdk 36. |
| A4 | HIGH (ops, no code) | device setup | Samsung "Put unused apps to sleep" and Android "Pause app activity if unused" can silently stop every alarm and broadcast. | The app is designed to be used through notifications, with almost no activity launches. One UI moves apps that are not opened for a few days to Sleeping or Deep sleeping apps, where background alarms and broadcasts are restricted or blocked. Android hibernation of unused apps puts the app in stopped state, and Android 15+ also cancels its PendingIntents. `USE_EXACT_ALARM` protects only from the AOSP restricted bucket, not from Samsung's lists. The spec makes battery prompts a non-goal, so this is a one-time owner step. |
| A5 | LOW | app/src/main/java/ar/sg/Sg.java:75-77 | The banner does not show when only the main channel is disabled. Features §3 says "(or channel disabled)". | `notif` = `areNotificationsEnabled()` only. A user who turns off "Recordatorio para dormir" gets no F1 and no banner. |
| A6 | LOW | app/src/main/java/ar/sg/MainActivity.java:223-229 | Java computes the F5 suggestion (`ref+60` rounded to nearest 15), against "C decides". It can differ from the C-computed suggestion in the F5 notification. | `int suggest = ((ref + 60 + STEP_MIN / 2) / STEP_MIN * STEP_MIN) % 1440;` |
| A7 | LOW | app/src/main/java/ar/sg/MainActivity.java:47; res/values/styles.xml:114-115 | Deprecated no-ops at API 35+: `setDecorFitsSystemWindows(false)`, `statusBarColor`, `navigationBarColor`. | javac `[deprecation]` warning. Edge-to-edge is enforced at targetSdk 36 anyway. Harmless. |
| A8 | LOW | app/src/main/java/ar/sg/MainActivity.java:350-352 | `fmtMod` prints `00:-1` for a `-1` (none) minute-of-day. It is only guarded for the next-alarm line. | Night rows and the jetlag line pass `ui[...]` values straight through. The C core should never send -1 there, so this is defensive only. |
| A9 | LOW | app/src/main/java/ar/sg/MainActivity.java:313-330 | The permission explanation dialog leaks on rotation (WindowLeaked in logcat) and is shown again, because `askedThisRun` is reset by recreation. | Not a crash. `SET_NOTIF_PROMPTED` still prevents re-asking once answered. |
| A10 | LOW | app/src/main/java/ar/sg/Sg.java:140-146 | F3 debounce uses `setExact` (not while-idle), as the spec chose. If the screen goes off and Samsung light-doze starts within the 90 s, the evaluation slips to the next maintenance window (minutes). | Accepted by spec §9. No change unless F3 lateness is observed. |
| A11 | LOW | app/src/main/cpp/sg_jni.c:142 | A failed `sg_store_save` is ignored. Memory is updated but the file is stale, so the change is lost on the next process start. There is no retry. | `(void)sg_store_save(...)`. Acceptable for v1. |
| A12 | LOW | ops | After `adb install`, the app is in stopped state: no BOOT/NEXT_ALARM broadcasts until it is opened once. After a force-stop, nothing runs until reopened (spec §3 accepts this). | Platform behaviour. |

Counts: BLOCKER 0, HIGH 1 (ops), MEDIUM 3, LOW 8.

## Fix instructions (apply literally)

### A1 — sg_jni.c: do not persist after an I/O load failure

In `app/src/main/cpp/sg_jni.c`, function `jni_init`, replace exactly these four lines:

```c
        code = sg_store_load(g_path, &g_state);
        g_ready = 1;
        g_loaded = 1;
        g_init_code = code;
```

with:

```c
        code = sg_store_load(g_path, &g_state);
        g_ready = 1;
        if (code == SG_STORE_E_IO) {
            /* File may exist but could not be read: defaults in memory, persistence
             * off, retry on the next nativeInit. Never overwrite the user's file. */
            g_loaded = 0;
        } else {
            g_loaded = 1;
            g_init_code = code;
        }
```

Nothing else changes. This compiles clean with the device warning flags. Every Java entry point calls `nativeInit` first, so the retry happens automatically on the next receiver or activity call.

### A2 — MainActivity: stop switch-state restore from calling into native before init

Two edits in `app/src/main/java/ar/sg/MainActivity.java`:

1. In `bindViews()`, directly after the line `swOnlyClock = findViewById(R.id.sw_only_clock);`, insert:

```java
        for (Switch sw : new Switch[]{swEnabled, swWinddown, swLate, swJetlag, swJetlagNoalarm, swOnlyClock}) {
            sw.setSaveEnabled(false);   // no restore-time onCheckedChanged before nativeInit
        }
```

2. In `onCreate`, directly after `setContentView(R.layout.activity_main);`, insert:

```java
        Sg.init(this);
```

(`bind()` in `onResume` always sets every switch from the C model, so nothing is lost by disabling the save.)

### A3 — MainActivity: apply all four insets including the cutout

In `onCreate`, replace:

```java
                android.graphics.Insets i = ins.getInsets(WindowInsets.Type.systemBars());
                v.setPadding(0, i.top, 0, i.bottom);
```

with:

```java
                android.graphics.Insets i = ins.getInsets(
                        WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                v.setPadding(i.left, i.top, i.right, i.bottom);
```

The 16 dp gutter stays on the inner `LinearLayout` (`paddingStart`/`paddingEnd`), so the visual side margin is unchanged in portrait.

### A4 — owner action on the phone (no code change; the spec forbids battery prompts)

Do this once after the first install, and again if reminders ever stop:
1. Settings > Battery > Background usage limits: turn off **Put unused apps to sleep**, or add Sueño-Guía to **Never sleeping apps**. Make sure it is not in Sleeping or Deep sleeping apps.
2. Settings > Apps > Sueño-Guía: turn off **Pause app activity if unused**. Under Battery, choose **Unrestricted**.
3. Open the app once after every `adb install` (see A12).

Optional, only if the owner wants it written down: add these three lines to the project README's install section. Do not add any in-app prompt.

### LOW items (optional; apply only if a later card has spare effort)

- A5: in `Sg.observe`, replace `int notif = nm.areNotificationsEnabled() ? 1 : 0;` with:
  ```java
  boolean on = nm.areNotificationsEnabled();
  NotificationChannel ch = nm.getNotificationChannel(CH_REMINDER);
  if (ch != null && ch.getImportance() == NotificationManager.IMPORTANCE_NONE) {
      on = false;
  }
  int notif = on ? 1 : 0;
  ```
- A7: delete the line `getWindow().setDecorFitsSystemWindows(false);   // edge-to-edge` (A3's fix makes it redundant).
- A8: at the top of `fmtMod`, add `if (mod < 0) return "—";`
- A6, A9, A10, A11, A12: no change now. A6 would need a new `SG_UI_*` slot from the C side, so it goes to the C-core auditor.
