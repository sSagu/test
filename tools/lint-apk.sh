#!/usr/bin/env bash
# Usage: tools/lint-apk.sh build/sg.apk
# Checks the built APK against docs/ADVICE-architecture.md section 6/7 (S1, S6, S7, 16 KB pages).
# Runs every check, prints PASS/FAIL per check, exits 1 if any failed.
set -uo pipefail

if [ -z "${BUILD_TOOLS:-}" ] || [ -z "${LLVM_BIN:-}" ]; then
  # shellcheck disable=SC1091
  . "${TOOLCHAIN_ENV:-/opt/android-sdk/toolchain.env}" || { echo "lint-apk: cannot source toolchain" >&2; exit 2; }
fi

APK="${1:-}"
[ -f "$APK" ] || { echo "usage: $0 <apk>  (file not found: '$APK')" >&2; exit 2; }

fails=0
ok()  { echo "PASS $*"; }
bad() { echo "FAIL $*"; fails=$((fails + 1)); }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
SO_PATH="lib/arm64-v8a/libsg.so"
SO="$WORK/libsg.so"

# --- size and layout -----------------------------------------------------
size=$(stat -c %s "$APK")
if [ "$size" -lt $((300 * 1024)) ]; then ok "apk size $size bytes < 300 KB"; else bad "apk size $size bytes >= 300 KB"; fi

entries=$(unzip -Z1 "$APK")
native=$(grep '^lib/' <<<"$entries" || true)
if [ "$native" = "$SO_PATH" ]; then ok "native libs: only $SO_PATH"; else bad "native libs are: ${native:-none} (want only $SO_PATH)"; fi

if unzip -p "$APK" "$SO_PATH" > "$SO" 2>/dev/null && [ -s "$SO" ]; then
  ok "extracted $SO_PATH"
else
  bad "cannot extract $SO_PATH from APK"; echo "lint: $fails failure(s)"; exit 1
fi

# Stored (uncompressed) .so: required by extractNativeLibs=false + zipalign -P 16.
if unzip -Zv "$APK" "$SO_PATH" | grep -q 'compression method:.*stored'; then ok ".so stored uncompressed"; else bad ".so is compressed"; fi

if "$BUILD_TOOLS/zipalign" -c -P 16 4 "$APK" >/dev/null 2>&1; then ok "zipalign -P 16 4 check on signed APK"; else bad "signed APK fails zipalign -P 16 check"; fi

# --- ELF checks -----------------------------------------------------------
hdr=$("$LLVM_BIN/llvm-readelf" -h "$SO")
if grep -q 'Machine:.*AArch64' <<<"$hdr"; then ok "ELF machine AArch64"; else bad "ELF machine is not AArch64"; fi

phdr=$("$LLVM_BIN/llvm-readelf" -lW "$SO")
dyn=$("$LLVM_BIN/llvm-readelf" -d "$SO")
notes=$("$LLVM_BIN/llvm-readelf" -n "$SO")

if grep -q 'GNU_RELRO' <<<"$phdr"; then ok "GNU_RELRO present"; else bad "GNU_RELRO missing"; fi

if grep -Eq 'BIND_NOW|FLAGS_1.*\bNOW\b' <<<"$dyn"; then ok "BIND_NOW set"; else bad "BIND_NOW not set"; fi

stack_flags=$(awk '/GNU_STACK/ {print $NF}' <<<"$phdr")
stack_exec=0
if [[ "$stack_flags" == 0x* ]]; then (( (stack_flags & 1) )) && stack_exec=1; elif [[ "$stack_flags" == *E* ]]; then stack_exec=1; fi
if [ -n "$stack_flags" ] && [ "$stack_exec" -eq 0 ]; then ok "GNU_STACK not executable ($stack_flags)"; else bad "GNU_STACK missing or executable ($stack_flags)"; fi

load_aligns=$(awk '/LOAD/ {print $NF}' <<<"$phdr" | sort -u | tr '\n' ' ')
if [ "$load_aligns" = "0x4000 " ]; then ok "all LOAD segments align 0x4000 (16 KB)"; else bad "LOAD alignments: '${load_aligns}' (want 0x4000 only)"; fi

if grep -Eq 'BTI' <<<"$notes" && grep -Eq '\bPAC\b' <<<"$notes"; then ok "GNU property: BTI, PAC"; else bad "GNU property note lacks BTI and/or PAC"; fi

# Exports: only JNI_OnLoad / JNI_OnUnload may be defined dynamic symbols.
exports=$("$LLVM_BIN/llvm-nm" -D --defined-only "$SO" | awk '{print $NF}' | grep -v '^$' | sort)
bad_exports=$(grep -vxE 'JNI_OnLoad|JNI_OnUnload' <<<"$exports" || true)
if grep -qx 'JNI_OnLoad' <<<"$exports" && [ -z "$bad_exports" ]; then
  ok "exports: $(echo $exports)"
else
  bad "unexpected exports: ${bad_exports:-none}; JNI_OnLoad present: $(grep -cx JNI_OnLoad <<<"$exports")"
fi

# --- manifest / badging ---------------------------------------------------
badging=$("$BUILD_TOOLS/aapt2" dump badging "$APK" 2>/dev/null)
if grep -q "minSdkVersion:'35'" <<<"$badging" && grep -q "targetSdkVersion:'36'" <<<"$badging"; then
  ok "minSdkVersion 35, targetSdkVersion 36"; else bad "sdk versions wrong (want minSdkVersion 35, targetSdkVersion 36)"; fi
if grep -q "native-code: 'arm64-v8a'" <<<"$badging"; then ok "native-code arm64-v8a only"; else bad "native-code is not arm64-v8a"; fi
if grep -q "name='android.permission.INTERNET'" <<<"$badging"; then bad "INTERNET permission present (S1)"; else ok "no INTERNET permission"; fi

xmltree=$("$BUILD_TOOLS/aapt2" dump xmltree --file AndroidManifest.xml "$APK" 2>/dev/null)
if grep -Eq 'allowBackup.*=false' <<<"$xmltree"; then ok "allowBackup=false"; else bad "allowBackup is not false (S7)"; fi

# Every receiver must be exported=false; only the launcher activity may be exported=true.
rx=$(awk '
  /E: receiver/ { if (blk) flush(); blk = 1; kind = "receiver"; ex = "none"; next }
  /E: activity/ { if (blk) flush(); blk = 1; kind = "activity"; ex = "none"; next }
  /E: / { if (blk) flush(); blk = 0; next }
  blk && /exported/ { ex = ($0 ~ /=true/) ? "true" : (($0 ~ /=false/) ? "false" : "other") }
  function flush() { print kind, ex }
  END { if (blk) flush() }
' <<<"$xmltree")
bad_rx=$(awk '$1=="receiver" && $2!="false" {print}' <<<"$rx")
if [ -n "$(awk '$1=="receiver"' <<<"$rx")" ] && [ -z "$bad_rx" ]; then ok "every receiver exported=false"; else bad "receiver export problems: ${bad_rx:-no receivers found}"; fi
if grep -Eq 'exported.*=true' <<<"$xmltree"; then
  n=$(grep -cE 'exported.*=true' <<<"$xmltree")
  if [ "$n" -eq 1 ] && awk '$1=="activity" && $2=="true"' <<<"$rx" | grep -q .; then
    ok "single exported=true component (launcher activity)"
  else
    bad "exported=true on $n component(s), expected only the launcher activity"
  fi
else
  bad "no exported=true found; launcher activity expected to be exported"
fi

# --- signature ------------------------------------------------------------
verify=$("$BUILD_TOOLS/apksigner" verify -v "$APK" 2>&1)
if [ $? -eq 0 ] && grep -q 'Verified using v3 scheme (APK Signature Scheme v3): true' <<<"$verify"; then
  ok "apksigner verify (v3): $(grep -o 'v2 scheme[^:]*: [a-z]*' <<<"$verify")"
else
  bad "apksigner verify failed"
fi

echo "lint: $fails failure(s)"
[ "$fails" -eq 0 ]
