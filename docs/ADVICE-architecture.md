# ADVICE-architecture: Sueño-Guía (package `ar.sg`, lib `libsg.so`)

Companion to docs/ADVICE-features.md (behaviour, strings, SG_* table). This file is the
build/structure/contract authority. Contract files already written by the architect and
NOT to be modified by operators (ask the orchestrator instead):
`app/src/main/cpp/include/*.h`, `app/src/main/AndroidManifest.xml`, `app/src/main/java/ar/sg/Native.java`.

## 1. Verified platform facts

| # | Fact | How verified |
|---|---|---|
| 1 | Galaxy S25 FE: Exynos 2400 (1×X4 + 5×A720 + 4×A520, ARMv9.2-A, arm64 only), 6.7" 1080×2340 AMOLED 120 Hz, ships Android 16 / One UI 8 (announced 2025-09-04). | GSMArena / 91mobiles spec pages (web search) |
| 2 | `ACTION_NEXT_ALARM_CLOCK_CHANGED`, `BOOT_COMPLETED`, `TIME_SET`, `TIMEZONE_CHANGED` are on the implicit-broadcast exemption list → manifest receivers work on API 26+. `MY_PACKAGE_REPLACED` is explicit to the package. | developer.android.com/guide/components/broadcast-exceptions |
| 3 | `android:exported="false"` receivers still get broadcasts "sent by the system"; `exported` only governs non-system sources. | receiver-element reference (web search) |
| 4 | `USE_EXACT_ALARM` (API 33+) is a normal permission, auto-granted, not user-revocable; apps holding it are exempt from the *restricted* standby bucket. `SCHEDULE_EXACT_ALARM` is denied by default on API 34+. Without either, `setExactAndAllowWhileIdle` throws SecurityException. | training/scheduling/alarms, topic/performance/appstandby (fetched) |
| 5 | Android 16 "safer intents" (explicit intent must match target filter) is **opt-in** via `android:intentMatchingFlags`; we do not opt in, but content intents still carry MAIN/LAUNCHER so they match anyway. Edge-to-edge cannot be opted out at targetSdk 36. | about/versions/16/behavior-changes-16 (fetched) |
| 6 | Android 17 (API 37) adds nothing alarm-related; it hardens BAL via IntentSender and requires `System.load()` libs to be read-only. We target 36 (the OS on the device; 37 opt-ins cannot be tested). compileSdk 37 from toolchain.env. | about/versions/17/behavior-changes-17 (fetched) |
| 7 | JDK 21 `javac` rejects `-bootclasspath` for target ≥ 9 and with `--release`; `-source 8 -target 8 -Xlint:-options -bootclasspath $ANDROID_JAR` compiles against android-37.2. `d8 --output` must be an existing directory. | tested locally (scratchpad) |
| 8 | NDK r30 clang 21 accepts `-march=armv9.2-a -mtune=cortex-a720 -mbranch-protection=standard -fstack-clash-protection -ftrivial-auto-var-init=zero -flto=thin -D_FORTIFY_SOURCE=3`; bionic cdefs honours `_FORTIFY_SOURCE>=3`; output .so has BTI+PAC notes, GNU_RELRO, BIND_NOW, GNU_STACK RW (no X), LOAD align 0x4000 (16 KB). | tested locally with llvm-readelf |
| 9 | MTE: Samsung firmware does not currently enable MTE for apps (One UI 9 may add a toggle). `android:memtagMode="async"` is a no-op where MTE is off and costs nothing; keep it. | web search (SamMobile/GrapheneOS forum) — low confidence, zero risk either way |
| 10 | Samsung Clock package is `com.sec.android.app.clockpackage`; `AlarmClockInfo.getShowIntent()` may be null → fail open (features advisor G.2). | features advisor; Samsung package name is well known |

## 2. Architecture

Rule: **C decides, Java executes.** Java never computes a time, never chooses a text,
never branches on app state. Every Java entry point does: take an observation snapshot →
call one `Native.*` method → run the returned command list. Framework APIs
(AlarmManager, NotificationManager) are called from Java, because a command list is
~40 lines of Java versus hundreds of lines of exception-checked JNI up-calls, and keeps the
C core 100 % host-testable. UI = one Java Activity with an XML layout (TextViews/switches);
the view model is produced by C (`nativeUiModel`), Java only formats numbers into
`strings.xml` templates.

```
 system: NEXT_ALARM_CLOCK_CHANGED / BOOT / TIME_SET / TZ / PKG_REPLACED
        │                         AlarmManager fires our PendingIntent(reqCode=alarmId)
        ▼                                     │                 notification action tap
 SystemReceiver ──┐                    AlarmReceiver ──┐                │
 (validate action)│                                    │          ActionReceiver
                  ▼                                    ▼                │
          Sg.observe(ctx) ──► SgObs{now, nextAlarmMs, creator, exact, notif}   ◄──┘
                  │
                  ▼  one JNI call, one mutex
          Native.nativeSync / nativeAlarmFired / nativeAction / nativeSet / nativeUiModel
                  │
                  ▼  sg_jni.c: load state once (sg_store_load), call sg_core_*,
                  │           sg_store_save if changed, flatten SgCmdList → long[]
                  ▼
          Sg.run(ctx, long[] cmds):  SCHEDULE → AlarmManager.setExactAndAllowWhileIdle
                                     CANCEL_ALARM → am.cancel(pi)
                                     NOTIFY → NotificationManager.notify(id, build(kind,variant,args))
                                     CANCEL_NOTIFY → nm.cancel(id)
 MainActivity.onResume: observe → nativeSync(APP_OPEN) → run; nativeUiModel → bind views.
```

## 3. Directory tree (owner card in brackets)

```
Makefile                         [B1] all targets, sources /opt/android-sdk/toolchain.env
.gitignore                       [B1] build/, keystore/, *.keystore, *.apk, *.idsig
tools/lint-apk.sh                [B1] readelf/aapt2 checks on the built APK
app/src/main/AndroidManifest.xml [ARCH, final]
app/src/main/cpp/include/sg_config.h sg_time.h sg_state.h sg_store.h sg_cmd.h sg_sleeplog.h sg_core.h [ARCH]
app/src/main/cpp/sg_time.c       [C1] local-time helpers
app/src/main/cpp/sg_state.c      [C1] defaults + sanitize
app/src/main/cpp/sg_sleeplog.c   [C1] ring buffer, week view, debt, F5 reference
app/src/main/cpp/sg_store.c      [C2] encode/decode/crc/atomic file I/O
app/src/main/cpp/sg_core.c       [C3] decision engine + sg_cmd_* helpers + ui model
app/src/main/cpp/sg_jni.c        [J1] JNI_OnLoad, RegisterNatives, mutex, state instance
app/src/main/java/ar/sg/Native.java      [ARCH, final]
app/src/main/java/ar/sg/Sg.java          [J1] observe(), run(), channels, PendingIntent factory
app/src/main/java/ar/sg/SystemReceiver.java [J1]
app/src/main/java/ar/sg/AlarmReceiver.java  [J1]
app/src/main/java/ar/sg/ActionReceiver.java [J1]
app/src/main/res/values/config.xml       [J1] string-array sg_clock_packages, channel ids
app/src/main/java/ar/sg/MainActivity.java [J2] screen + settings + permission prompt
app/src/main/res/layout/activity_main.xml [J2]
app/src/main/res/values/strings.xml       [J2] exactly the table in ADVICE-features §5
app/src/main/res/values/styles.xml colors.xml dimens.xml [J2] true-black theme
app/src/main/res/drawable/ic_moon.xml     [J2] monochrome vector (notification small icon)
app/src/main/res/mipmap-anydpi-v26/ic_launcher.xml + drawable/ic_launcher_fg.xml [J2]
app/src/main/res/xml/data_extraction_rules.xml [J2] exclude everything (cloud + d2d)
tests/test_main.c tests/test_time.c tests/test_sleeplog.c tests/test_store.c tests/test_core.c [T1]
tests/sg_test.h                  [T1] tiny assert macros (no framework)
docs/*.md                        [advisors]
```

## 4. JNI contract

Registered in `JNI_OnLoad` via `RegisterNatives` on class `ar/sg/Native`; no exported
`Java_*` symbols (`-fvisibility=hidden`; only `JNI_OnLoad`/`JNI_OnUnload` are `JNIEXPORT`).
`JNI_OnLoad` returns `JNI_VERSION_1_6`, caches nothing but the mutex; it does **not** hold a
`jclass` global ref (no C→Java calls exist, so none is needed; `JNI_OnUnload` just destroys
the mutex).

| Java (static, class ar.sg.Native) | JNI descriptor | C entry (sg_jni.c) | calls |
|---|---|---|---|
| `int nativeInit(String)` | `(Ljava/lang/String;)I` | `jni_init` | `sg_store_load` (once; later calls return cached code), stores path copy (≤ SG_STORE_PATH_MAX) |
| `long[] nativeSync(JJIIII)` | `(JJIIII)[J` | `jni_sync` | `sg_core_sync` |
| `long[] nativeAlarmFired(JJIIII)` | `(JJIIII)[J` | `jni_alarm_fired` | `sg_core_alarm_fired` |
| `long[] nativeAction(JJIIII)` | `(JJIIII)[J` | `jni_action` | `sg_core_action` |
| `long[] nativeSet(JJIIIII)` | `(JJIIIII)[J` | `jni_set` | `sg_core_set` |
| `int nativeGet(I)` | `(I)I` | `jni_get` | `sg_core_get` |
| `long[] nativeUiModel(JJIII)` | `(JJIII)[J` | `jni_ui_model` | `sg_core_ui` + `sg_core_ui_flatten` |

The 5 leading scalars are always `(nowMs, nextAlarmMs, creator, exactAllowed, notifAllowed)`
→ `SgObs`. Each mutating entry: lock → copy state → call → if `memcmp` differs, `sg_store_save`
→ unlock → `NewLongArray(n)` + `SetLongArrayRegion` → return (NULL on allocation failure,
Java treats NULL as empty). No C→Java calls. No `JNIEnv` is stored.

Java-side mapping (Sg.java): `alarmId → PendingIntent.getBroadcast(ctx, alarmId, new Intent(ctx, AlarmReceiver.class).setAction("ar.sg.ALARM").putExtra("id", alarmId), FLAG_IMMUTABLE|FLAG_UPDATE_CURRENT)`; action buttons: `getBroadcast(ctx, 100+actionId, new Intent(ctx, ActionReceiver.class).setAction("ar.sg.ACTION").putExtra("id", actionId), FLAG_IMMUTABLE|FLAG_UPDATE_CURRENT)`; content intent: `getActivity(ctx, 200, new Intent(ctx, MainActivity.class).setAction(ACTION_MAIN).addCategory(CATEGORY_LAUNCHER), FLAG_IMMUTABLE)`. Notification kind→(channel, id): F1→`sg_reminder`/1001, F2→`sg_winddown`/1002, F3→`sg_late`/1003, F5→`sg_hints`/1005. Text: `(kind, variant)` → `(title, body)` resource pair per sg_cmd.h comments; times formatted with `str_fmt_time(mod/60, mod%60)`, durations with `str_fmt_duration(min/60, min%60)` (or `str_fmt_duration_min` when < 60).

## 5. Persistence

Spec is the header comment of `sg_store.h` (byte offsets, CRC, atomic write, corruption
handling). Summary: fixed 2304-byte little-endian image, magic `SGS1`, version 1, CRC32 over
bytes [16, 2304). Decode never trusts counts: ring indices, flags, and settings pass through
`sg_state_sanitize`. Corrupt/short/wrong-version file → defaults in memory, file renamed
`.bad`, next save overwrites. Version bump = new offsets table + a decode branch; never
reinterpret v1 bytes as v2. File mode 0600 under `getFilesDir()` (app-private, not backed up).

## 6. Build (plain Makefile, no Gradle)

`Makefile` starts with `include /opt/android-sdk/toolchain.env` after stripping `export `
(use `$(shell sed 's/^export //' /opt/android-sdk/toolchain.env)` into a generated
`build/toolchain.mk`, or `-include` after `sed`); every tool path is `$(BUILD_TOOLS)/…`,
`$(NDK_CLANG)`, `$(LLVM_BIN)/…`, `$(ANDROID_JAR)`. Nothing hard-coded.

Targets: `make apk` (default), `make test` (host, ASan+UBSan), `make asan` (alias), `make valgrind`,
`make lint`, `make clean`. Output under `build/`.

Device .so (`build/lib/arm64-v8a/libsg.so`), sources `app/src/main/cpp/*.c`:
```
CFLAGS_DEV = -std=c11 -O2 -flto=thin -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections \
  -march=armv9.2-a -mtune=cortex-a720 -mbranch-protection=standard \
  -fstack-protector-strong -fstack-clash-protection -ftrivial-auto-var-init=zero \
  -D_FORTIFY_SOURCE=3 -DNDEBUG -Wall -Wextra -Werror -Wformat=2 -Wconversion -Wshadow \
  -Wvla -Wimplicit-fallthrough -Iapp/src/main/cpp/include
LDFLAGS_DEV = -shared -flto=thin -Wl,-z,relro,-z,now,-z,noexecstack,-z,max-page-size=16384 \
  -Wl,--gc-sections,--build-id=sha1,--no-undefined -Wl,-soname,libsg.so -s -llog
```
Host (clang, `build/host/`): `-std=c11 -O1 -g -Wall -Wextra -Werror -Wformat=2 -Wconversion -Wshadow -Wvla -fstack-protector-strong -D_FORTIFY_SOURCE=2 -I…/include`; `test`/`asan` add `-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer`; `valgrind` builds without sanitizers and runs `valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=1 build/host/sg_test_plain`. Tests run with `TZ=America/Argentina/Buenos_Aires` by default and additionally `TZ=Europe/Madrid` (DST) and `TZ=UTC`.

APK pipeline (`make apk`):
1. `$(NDK_CLANG) $(CFLAGS_DEV) -c` each .c → `build/obj/*.o`; link → `build/lib/arm64-v8a/libsg.so`.
2. `$(BUILD_TOOLS)/aapt2 compile --dir app/src/main/res -o build/res.zip`.
3. `$(BUILD_TOOLS)/aapt2 link -o build/base.apk --manifest app/src/main/AndroidManifest.xml -I $(ANDROID_JAR) --min-sdk-version 35 --target-sdk-version 36 --java build/gen build/res.zip` (generates `build/gen/ar/sg/R.java`).
4. `javac -source 8 -target 8 -Xlint:-options -bootclasspath $(ANDROID_JAR) -d build/classes build/gen/ar/sg/R.java app/src/main/java/ar/sg/*.java`.
5. `mkdir -p build/dex && $(BUILD_TOOLS)/d8 --release --min-api 35 --output build/dex $(find build/classes -name '*.class')`.
6. `cp build/base.apk build/unaligned.apk && (cd build && zip -j unaligned.apk dex/classes.dex && zip unaligned.apk lib/arm64-v8a/libsg.so)` (library path inside zip must be exactly `lib/arm64-v8a/libsg.so`; `extractNativeLibs=false` requires it stored uncompressed: use `zip -0` for the .so).
7. `$(BUILD_TOOLS)/zipalign -P 16 -f -v 4 build/unaligned.apk build/aligned.apk` (`-P 16` aligns uncompressed .so to 16 KB; verified flag exists in build-tools 37); then `$(BUILD_TOOLS)/zipalign -c -P 16 -v 4 build/aligned.apk` must exit 0.
8. Keystore: if `keystore/debug.keystore` is missing, `keytool -genkeypair -keystore keystore/debug.keystore -storepass android -keypass android -alias sgdebug -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=SG Debug"`. Then `$(BUILD_TOOLS)/apksigner sign --ks keystore/debug.keystore --ks-pass pass:android --ks-key-alias sgdebug --v2-signing-enabled true --v3-signing-enabled true --out build/sg.apk build/aligned.apk` and `apksigner verify --print-certs build/sg.apk`.

`make lint`: `clang-tidy` on `app/src/main/cpp/*.c` with host flags (checks: `bugprone-*,cert-*,clang-analyzer-*,misc-*,-misc-no-recursion,readability-*`, warnings as errors), `cppcheck --error-exitcode=1 --enable=warning,performance,portability` if installed, then `tools/lint-apk.sh build/sg.apk`, which unzips the .so and asserts with `$(LLVM_BIN)/llvm-readelf`: `GNU_RELRO` present; `BIND_NOW` in dynamic flags; `GNU_STACK` without `E`; all `LOAD` Align == 0x4000; `.note.gnu.property` shows `BTI, PAC`; `llvm-nm -D` exports only `JNI_OnLoad JNI_OnUnload`; `aapt2 dump badging` shows `sdkVersion:'35' targetSdkVersion:'36'`, `native-code: 'arm64-v8a'`, no `android.permission.INTERNET`; `aapt2 dump xmltree --file AndroidManifest.xml` shows `allowBackup=false` and every receiver `exported=false`; `apksigner verify` passes; APK size < 300 KB.

## 7. Checklists (the auditor grades against these numbers)

Security
S1. No `INTERNET`, no services, no content providers, no `exported="true"` except the launcher Activity.
S2. Every receiver's `onReceive` first checks `intent != null && expectedAction.equals(intent.getAction())`; otherwise `return`. AlarmReceiver/ActionReceiver also validate the `id` extra range (1..7 / 1..2).
S3. All PendingIntents: explicit component, `FLAG_IMMUTABLE`, distinct request codes (alarms 1–7, actions 101–102, content 200).
S4. State file: path from `getFilesDir()` only; opened with `O_CLOEXEC|O_NOFOLLOW`, mode 0600, fstat regular-file check, exact-size read, CRC verified, everything sanitized (`sg_state_sanitize`) before use. No `strcpy`/`sprintf`/`strcat`; `snprintf` with size checks only.
S5. All integer math on `int64_t` ms with explicit casts; minutes never overflow (`sg_time_diff_min` saturates). No signed overflow (UBSan enforces in tests).
S6. Device build uses exactly `CFLAGS_DEV`/`LDFLAGS_DEV` above; `make lint` proves RELRO/NOW/NX/BTI+PAC/16 KB/exports.
S7. Manifest unchanged by operators; `allowBackup=false` + `dataExtractionRules` exclude all.
S8. No logging of state contents on device (`__android_log_print` only for error codes, never times or counts, and only at `ANDROID_LOG_WARN`+).
S9. Never call `setAlarmClock`. Only `setExactAndAllowWhileIdle`, `setAndAllowWhileIdle`, `setExact`, `set`, `cancel`.

Memory
M1. No `malloc`/`calloc`/`strdup` anywhere in `app/src/main/cpp` (grep is part of `make lint`). All buffers are fixed-size locals or the single static `SgState`.
M2. JNI: `GetStringUTFChars` paired with `ReleaseStringUTFChars` on every path; `NewLongArray` result checked for NULL and `ExceptionCheck()` consulted after every `New*`/`Set*` call; on exception → `ExceptionClear()` and return NULL. No local refs created in loops. No global refs. No cached `JNIEnv`.
M3. Mutex (`pthread_mutex_t`, static initializer) held for the whole native call; never across a JNI call that can throw (array creation happens after unlock, from a stack copy).
M4. `SgCmdList.dropped` must be 0 in all tests; core asserts `count <= SG_MAX_CMDS`.
M5. Host tests pass under `-fsanitize=address,undefined` and under valgrind with zero errors and zero leaks ("definitely/indirectly/possibly lost: 0 bytes").
M6. No recursion, no VLAs (`-Wvla`), no `alloca`.

## 8. Operator task cards (parallel; file ownership is disjoint)

Common to every card: read `docs/ADVICE-features.md` §2–§4 and §7, this file §2 and §7, and
all headers in `app/src/main/cpp/include/`. Do not edit headers/manifest/Native.java. Use
absolute paths. Do not commit.

### B1 — Build system (effort: medium)
Owns: `Makefile`, `.gitignore`, `tools/lint-apk.sh`. Read first: §6 above, `/opt/android-sdk/toolchain.env`.
Do: implement every target in §6 exactly; `make test` must compile `app/src/main/cpp/sg_time.c sg_state.c sg_sleeplog.c sg_store.c sg_core.c` + `tests/*.c` (exclude `sg_jni.c` on host) into `build/host/sg_test_asan` and run it under the three TZ values; `make valgrind` builds `build/host/sg_test_plain` and runs valgrind; `make lint` includes `! grep -rE '\b(malloc|calloc|realloc|strdup|strcpy|strcat|sprintf|alloca)\s*\(' app/src/main/cpp`. Makefile must work before other cards finish: missing .c files → clear error, not a cryptic one (use `$(wildcard)` and a check). `.gitignore`: `build/`, `keystore/`, `*.apk`, `*.idsig`, `*.keystore`.
Accept: `make -n apk` prints the pipeline with no hard-coded version strings; `grep -c '37.0.0\|30.0.16248370\|android-37' Makefile` → 0; once C1–C3+T1 exist: `make test && make valgrind && make lint` exit 0; `make apk` produces `build/sg.apk` and `tools/lint-apk.sh build/sg.apk` exits 0; `make clean && git status --porcelain` shows no `build/` or keystore files.

### C1 — Time, state defaults, sleep log (effort: medium)
Owns: `app/src/main/cpp/sg_time.c`, `sg_state.c`, `sg_sleeplog.c`. Read first: `sg_time.h`, `sg_state.h`, `sg_sleeplog.h`, features F4/F5.
Do: implement every prototype with the documented semantics. `sg_time_local` uses `time_t secs = ms/1000` (floor for negatives not needed: range ≥ 0) + `localtime_r`; `sg_time_from_local` builds `struct tm` with `tm_isdst=-1` and `mktime`; `sg_time_date_add` goes through `mktime` normalisation at 12:00 local to dodge DST gaps. `sg_log_week`: build the 7 dates `today-6..today`, attribute records as documented, newest record per date wins, `est_sleep` via `sg_log_est_sleep_min`, debt = `max(0, Σ(target - est))` over status==1 nights. Median: copy ≤10 samples to a local array, insertion sort, lower median.
Accept: `clang -std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow -Wvla -fsyntax-only -Iapp/src/main/cpp/include app/src/main/cpp/sg_time.c app/src/main/cpp/sg_state.c app/src/main/cpp/sg_sleeplog.c` exits 0; same with `$NDK_CLANG`; T1's `test_time` and `test_sleeplog` pass under `make test` and `make valgrind`.

### C2 — Persistence (effort: low)
Owns: `app/src/main/cpp/sg_store.c`. Read first: `sg_store.h` (the format table is normative), `sg_state.h`.
Do: byte-wise LE put/get helpers (`static inline void put_u16(uint8_t *p, uint16_t v)` etc.); encode/decode exactly per offset table, zero padding; CRC32 bitwise (no table needed); `sg_store_load`: `open(O_RDONLY|O_CLOEXEC|O_NOFOLLOW)`, `fstat` → `S_ISREG` and `st_size == SG_STORE_SIZE` else corrupt, loop `read` until 2304 or error, `close`, `sg_store_decode`; on corrupt: `rename(path, path.bad)` best effort. `sg_store_save`: build `tmp = "<path>.tmp"` with `snprintf` into `char[SG_STORE_PATH_MAX+8]` (fail `SG_STORE_E_ARG` if truncated), `open(O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC, 0600)`, full `write` loop, `fsync`, `close`, `rename`, then open parent dir (`O_RDONLY|O_DIRECTORY`) and `fsync` it (ignore EINVAL). Any failure → `unlink(tmp)`, return `SG_STORE_E_IO`. Handle `EINTR` on read/write.
Accept: `clang … -fsyntax-only sg_store.c` exits 0 on host and NDK; T1 `test_store` passes under ASan/UBSan and valgrind; `strings`-free check: `! grep -E 'malloc|sprintf\(|strcpy' app/src/main/cpp/sg_store.c`.

### C3 — Decision engine (effort: medium)
Owns: `app/src/main/cpp/sg_core.c` (includes `sg_cmd_*` helpers). Read first: `sg_core.h` header comment (algorithm), `sg_cmd.h`, features §2/§3 tables.
Do, precisely:
- `sg_core_relevant`: order of checks NONE → OTHER_APP (`creator==OTHER && only_clock`) → WINDOW (`sg_time_local(T).mod` outside [240,720]) → HORIZON (`T-now > 36h`) → OK.
- `sg_core_sync` per header steps 1–6. F1 scheduling: `F1_at = T - lead`; if `now < F1_at` → `SCHEDULE(F1, F1_at, EXACT_IDLE)` and, if winddown on and `now < F1_at - winddown`, `SCHEDULE(F2, F1_at - winddown, EXACT_IDLE)`; if `F1_at <= now <= F1_at + grace` and `last_f1_for_T != T` → `SCHEDULE(F1, now, EXACT_IDLE)` (fires immediately; Java treats past times normally); else (later than grace, `late_on`, T changed) → set `debounce_due_ms = now + 90 s`, `debounce_T = T`, `SCHEDULE(DEBOUNCE, due, EXACT)`. Always `SCHEDULE(RECHECK, now + SG_RECHECK_MIN, INEXACT_IDLE)` when enabled; `F5_NOALARM` at the next Fri/Sat `SG_NOALARM_HOUR_MIN` when `jetlag_on && jetlag_noalarm`; `HOUSEKEEP` at `wake_ms + 1 min` if a record is open with wake_ms > now. On T change: `CANCEL_NOTIFY(F1)` if `SG_FLAG_F1_POSTED`, `CANCEL_NOTIFY(F2)`, `CANCEL_ALARM(F1,F2,F5_HINT)`, `snooze_count=0`, `sg_log_follow_alarm`. Reason `TZ_CHANGED` → `sg_ref_clear`. If T is irrelevant: cancel F1/F2/F5_HINT alarms and F1/F2 notifications; `last_seen_T = 0`.
- `sg_core_alarm_fired`: `F1` → if relevant and `T == last_seen_T`: post F1 (`SG_TXT_F1_NORMAL` with `bed_mod = local(T - (target+latency)).mod` when that instant is in the future, else `SG_TXT_F1_LATE` with avail), actions `SLEEP | (snooze_count < SG_SNOOZE_MAX ? SNOOZE : 0)`, timeout `SG_F1_TIMEOUT_MIN`; set `last_f1_for_T=T`, `last_notified_ms=now`, `F1_POSTED`; `CANCEL_NOTIFY(F2)`; add F5 weekday sample if wake date is Mon–Fri; if wake date is Sat/Sun, ref valid, `delta = mod(T) - ref > SG_JETLAG_TRIGGER_MIN`, `last_f5_for_T != T` → `SCHEDULE(F5_HINT, now + 1 min, EXACT_IDLE)`. `F2` → post F2 (`SG_TXT_F2`, no actions, timeout 60) once per T. `DEBOUNCE` → if `now >= debounce_due_ms` and current T still == `debounce_T` and relevant: `avail = sg_core_avail_min`; post F3 only if `avail >= SG_LATE_MIN_SLEEP_MIN` and `now - last_notified_ms >= SG_COOLDOWN_MIN` and not (`avail >= target && F1_at > now`); variant `F3_OK` when `avail >= target` else `F3_LATE`; actions `SLEEP`; timeout 120; update `last_notified_ms`; clear debounce fields either way. `F5_HINT` → post `SG_TXT_F5_ALARM` (alarm_mod, delta, `sg_time_round_mod(ref + SG_JETLAG_SUGGEST_MIN, 15)`), `last_f5_for_T = T`. `F5_NOALARM` → if today is Fri/Sat, no relevant T before 12:00 next day, ref valid, `last_f5_date != today` → post `SG_TXT_F5_NOALARM`, set date. `RECHECK`/`HOUSEKEEP` → behave as `sg_core_sync(reason RECHECK)`.
- `sg_core_action`: `SLEEP` → valid iff relevant T and `now ∈ [T - lead - 120 min, T - 60 min]`; then `sg_log_bed_tap(now, T)` (and F5 sample if Mon–Fri wake); always `CANCEL_NOTIFY(F1)`, `CANCEL_NOTIFY(F3)`, clear `F1_POSTED`. `SNOOZE` → if `snooze_count < SG_SNOOZE_MAX`: `snooze_count++`, `CANCEL_NOTIFY(F1)`, `SCHEDULE(F1, now + 15 min, EXACT_IDLE)`.
- `sg_core_set`: clamp (`lead` to [480,660] step 15 via `v - (v - min) % step`; same pattern for others; flags to 0/1), `CLEAR_LOG` → `sg_log_clear`; then run sync. `sg_core_ui`: fill all fields; `jetlag_status`: 2 if `!jetlag_on`, 0 if ref invalid, 3 if relevant T is Sat/Sun with delta > trigger, else 1.
Accept: `clang … -fsyntax-only sg_core.c` host + NDK exit 0; T1 `test_core` passes under ASan/UBSan and valgrind; `grep -c 'dropped' app/src/main/cpp/sg_core.c` ≥ 1 (full-list handling present).

### J1 — JNI glue, receivers, command executor (effort: medium)
Owns: `app/src/main/cpp/sg_jni.c`, `app/src/main/java/ar/sg/Sg.java`, `SystemReceiver.java`, `AlarmReceiver.java`, `ActionReceiver.java`, `res/values/config.xml`. Read first: §4, §7 M2–M3, S2–S3, `Native.java`, `sg_cmd.h`.
Do: `sg_jni.c` as specified in §4 (static `SgState g_state`, `char g_path[SG_STORE_PATH_MAX]`, `int g_loaded`, `pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER`; `JNI_OnLoad`: `GetEnv`, `FindClass("ar/sg/Native")`, `RegisterNatives` with the 7 descriptors, `DeleteLocalRef`, return `JNI_VERSION_1_6` or `JNI_ERR`). `Sg.java`: `static Observation observe(Context)` = `System.currentTimeMillis()`, `AlarmManager.getNextAlarmClock()` (null → 0/CREATOR_NONE; else trigger time + `getShowIntent()==null ? CREATOR_NONE : (Arrays.asList(getResources().getStringArray(R.array.sg_clock_packages)).contains(showIntent.getCreatorPackage()) ? CREATOR_ALLOWED : CREATOR_OTHER)`), `canScheduleExactAlarms()`, `NotificationManager.areNotificationsEnabled()`; `static void run(Context, long[] cmds)` executes commands per §2/§4 with `try/catch (SecurityException)` falling back from exact to `setAndAllowWhileIdle` / `set`; `ensureChannels(Context)` creates the 4 channels from features §4 (idempotent, called before any notify); notification builder per features §4 (`setSmallIcon(R.drawable.ic_moon)`, `setCategory(CATEGORY_REMINDER)`, `setVisibility(VISIBILITY_PUBLIC)`, `setAutoCancel(true)`, `setTimeoutAfter(a6 * 60000L)`, `BigTextStyle`). Receivers: validate action per S2, call `Native.nativeInit(ctx.getFilesDir() + "/sg_state.bin")`, map action → reason (`NEXT_ALARM_CLOCK_CHANGED`→BROADCAST, `BOOT_COMPLETED`→BOOT, `TIME_SET`→TIME_SET, `TIMEZONE_CHANGED`→TZ_CHANGED, `MY_PACKAGE_REPLACED`→PKG_REPLACED), then `Sg.run(ctx, Native.nativeSync(...))`. No `goAsync`, no threads, no wakelocks (all calls are sub-millisecond). `config.xml`: `<string-array name="sg_clock_packages">` with the two packages.
Accept: `$NDK_CLANG $CFLAGS_DEV -c app/src/main/cpp/sg_jni.c -o /dev/null` exits 0 (use flags from Makefile §6); `javac -source 8 -target 8 -Xlint:-options -bootclasspath $ANDROID_JAR -d /tmp/x app/src/main/java/ar/sg/*.java` exits 0 given a stub `R.java` or after `make apk` step 3; `grep -c 'ExceptionCheck' sg_jni.c` ≥ 3; `grep -c 'ReleaseStringUTFChars' sg_jni.c` ≥ 1; `! grep -E 'setAlarmClock|INTERNET|goAsync|WakeLock' -r app/src/main/java`; every receiver file contains `getAction()` check before any work.

### J2 — Screen, theme, resources (effort: medium)
Owns: `MainActivity.java`, `res/layout/activity_main.xml`, `res/values/strings.xml`, `styles.xml`, `colors.xml`, `dimens.xml`, `res/drawable/*`, `res/mipmap-anydpi-v26/*`, `res/xml/data_extraction_rules.xml`. Read first: features §5 (strings, copy verbatim incl. `%1$s` placeholders), features §2 F4 screen list, `Native.java` UI indices.
Do: `strings.xml` = exactly the §5 table (plus `str_wday_short` string-array `dom,lun,mar,mié,jue,vie,sáb`). Theme `Theme.SG` parent `android:Theme.DeviceDefault.NoActionBar`, `android:windowBackground=#000000`, `colorBackground #000000`, text `#E6E6E6`, accent `#8AB4F8`, `android:windowLightStatusBar=false`, `android:enforceStatusBarContrast=false`; `isLightTheme=false`. Layout: `ScrollView` root, 16 dp side padding, `minHeight 48dp` on every clickable, text 16–18 sp; sections: next alarm, next reminder, in-bed suggestion, 7 night rows, debt, jetlag line, disclaimer, settings (switches + `+/−` buttons for lead/target/winddown stepping by 15, "Borrar registro" with `AlertDialog`), notification banner with "Abrir ajustes" → `Settings.ACTION_APP_NOTIFICATION_SETTINGS`. Edge-to-edge: `ViewCompat`-free — `root.setOnApplyWindowInsetsListener((v, ins) -> { Insets i = ins.getInsets(WindowInsets.Type.systemBars()); v.setPadding(16dp, i.top, 16dp, i.bottom); return ins; })`. `onResume`: `Sg.ensureChannels`, `nativeInit`, `Sg.run(nativeSync(APP_OPEN))`, then `bind(nativeUiModel(...))`; first-launch permission flow: if `nativeGet(SET_NOTIF_PROMPTED)==0` show explanation dialog (`str_perm_notif_*`) → `requestPermissions(POST_NOTIFICATIONS)` → `nativeSet(SET_NOTIF_PROMPTED,1)`. All numbers formatted with `getString(R.string.str_fmt_*)`; `-1` fields render the `*_none/empty` strings. `data_extraction_rules.xml`: `<cloud-backup><exclude domain="root"/>…` and `<device-transfer>` same, exclude `file`, `database`, `sharedpref`, `external`, `root` domains. `ic_moon.xml`: 24 dp monochrome vector path, `android:tint="#FFFFFF"`. Launcher icon: adaptive, background `#000000`, foreground the moon.
Accept: `$BUILD_TOOLS/aapt2 compile --dir app/src/main/res -o /tmp/r.zip && $BUILD_TOOLS/aapt2 link -o /tmp/b.apk --manifest app/src/main/AndroidManifest.xml -I $ANDROID_JAR --min-sdk-version 35 --target-sdk-version 36 --java /tmp/gen /tmp/r.zip` exits 0; `javac` (as in J1) of `MainActivity.java` + generated `R.java` + `Native.java` + a stub `Sg.java` if J1 is not done, exits 0; `grep -c '<string name="str_' app/src/main/res/values/strings.xml` equals the row count of features §5 (67); `! grep -nE 'Thread|Handler|Timer' MainActivity.java`.

### T1 — Host unit tests (effort: medium)
Owns: `tests/sg_test.h`, `tests/test_main.c`, `tests/test_time.c`, `tests/test_sleeplog.c`, `tests/test_store.c`, `tests/test_core.c`. Read first: all headers, features §2/§3, §7 M4–M5.
`sg_test.h`: `SG_CHECK(cond)` prints file:line and increments a failure counter; `test_main.c` runs every `test_*` function and returns non-zero on failure. Helper `int64_t at(const char *date, int mod)` = `sg_time_from_local(...)`. Tests use a fresh `SgState` from `sg_state_defaults` and an `SgObs` each; assert on `SgCmdList` contents with a helper `find_cmd(list, type, a0)`. Required cases (each its own function):
1. time: `sg_time_local` round-trip with `sg_time_from_local` for 00:00, 23:59, across midnight; DST spring-forward and fall-back dates under `TZ=Europe/Madrid` (2026-03-29 02:30 non-existent → normalised; 2026-10-25 02:30 ambiguous) and `TZ=America/Argentina/Buenos_Aires` (no DST); `sg_time_date_add` over month/year ends and ±400; `sg_time_floor_min`, `sg_time_round_mod` wrap at 1440; `sg_time_diff_min` saturation.
2. core: alarm 07:00 with lead 570 → F1 scheduled at 21:30 previous day (crosses midnight); alarm at 23:00 → `REL_WINDOW` and nothing scheduled except RECHECK; T 40 h away → `REL_HORIZON`; no alarm → cancels; `creator OTHER` + only_clock → `REL_OTHER_APP`, and with `only_clock=0` → OK.
3. alarm < lead: now 23:00, alarm 06:00 (avail 400 min) → DEBOUNCE scheduled, then `alarm_fired(DEBOUNCE)` at due → F3 `F3_LATE` with a3 == 390 (7 h − 20 min floored to 15); second change within cooldown → no F3; avail < 240 → no F3; avail ≥ target with F1 future → no F3.
4. debounce: three syncs 10 s apart each re-issue DEBOUNCE with later due; firing before due does nothing; firing after T changed again does nothing.
5. F1 fired: NOTIFY F1 with correct `alarm_mod`, `bed_mod`, actions SLEEP|SNOOZE; second `alarm_fired(F1)` for the same T → no second NOTIFY; snooze twice then `SNOOZE` action a third time → no reschedule; grace: sync at F1_at+10 min → immediate F1; at F1_at+40 min → debounce path.
6. F2: off by default → never scheduled; on → scheduled at F1_at−30; not scheduled when now ≥ F1_at.
7. F4: SLEEP inside window → record added; outside → ignored but notification cancelled; dedup within 30 min updates; `follow_alarm` on T change; `close_if_due`; `sg_log_week` attributes nights to wake date, 7-day window, debt = Σ(target − est) clamped at 0, surplus offsets deficit, `logged_count`; ring buffer wrap at 90 (add 95 records, oldest evicted, order preserved).
8. F5: 3 weekday samples → median; Saturday alarm 120 min later → F5_HINT scheduled 1 min after F1 fires and NOTIFY `F5_ALARM` with suggest = round(ref+60); Monday wake never triggers; TZ_CHANGED clears ref; no-alarm Friday 21:30 with valid ref → `F5_NOALARM` NOTIFY once per date.
9. store: encode→decode round-trip equals (memcmp after sanitize); file save→load round-trip in a `mkdtemp` dir; truncated file (2303 bytes) → `E_CORRUPT` + defaults + `.bad` exists; flipped byte → `E_CORRUPT`; wrong magic/version → `E_CORRUPT`; missing file → `E_NOFILE`; `night_count=200` in image → sanitized to empty; path too long → `E_ARG`; tmp file never left behind.
10. commands: `dropped == 0` in every test; flatten length == count*8; `sg_core_ui_flatten` indices match `SG_UI_*`.
Accept: `make test` (ASan+UBSan, three TZs) and `make valgrind` exit 0; `grep -c '^static void test_' tests/*.c | awk -F: '{s+=$2} END{print s}'` ≥ 30.

## 9. Decisions worth knowing

- Scheduling: `USE_EXACT_ALARM` + `setExactAndAllowWhileIdle`, as the features advisor chose; it also keeps the app out of the restricted bucket (fact 4), which answers the Samsung "deep sleep" concern without a battery-optimization prompt (a non-goal in features §6). Debounce uses plain `setExact` (device is awake while the user edits alarms; avoids the 9-min while-idle throttle). The 60-min safety re-check is an inexact, batched alarm re-armed only while `enabled`; it is the only non-event-driven work and costs one batched wakeup per hour.
- UI: Java Activity + XML. `NativeActivity` would need a GL/Canvas text stack for no benefit; JNI-driven framework views would triple the JNI surface.
- No CFI (`-fsanitize=cfi`): the only indirect calls are ART→native, which CFI cannot check; PAC/BTI via `-mbranch-protection=standard` covers the device.
- `-Wconversion` is on with `-Werror`; operators cast explicitly at every narrowing.
