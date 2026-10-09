# Sueño-Guía build. Plain Makefile, no Gradle.
# Toolchain versions come only from $(TOOLCHAIN_ENV); nothing version-specific is written here.

.DEFAULT_GOAL := apk
SHELL := /bin/bash
.SHELLFLAGS := -o pipefail -c

TOOLCHAIN_ENV ?= /opt/android-sdk/toolchain.env
TOOLCHAIN_MK  := build/toolchain.mk
$(shell mkdir -p build && { [ -f "$(TOOLCHAIN_ENV)" ] && sed 's/^export //' "$(TOOLCHAIN_ENV)" > $(TOOLCHAIN_MK); } 2>/dev/null)
-include $(TOOLCHAIN_MK)

ifneq ($(filter-out clean,$(or $(MAKECMDGOALS),apk)),)
ifeq ($(strip $(BUILD_TOOLS)),)
$(error BUILD_TOOLS unset: $(TOOLCHAIN_ENV) missing or unreadable. Install the Android SDK/NDK or set TOOLCHAIN_ENV=/path/to/toolchain.env)
endif
endif

# Host compiler for tests and lint. gcc has working ASan/UBSan runtimes here; the NDK clang is device-only.
HOST_CC ?= gcc
HOST_TIDY_CC ?= clang
TEST_TZS ?= America/Argentina/Buenos_Aires Europe/Madrid UTC

BUILD    := build
SRC_DIR  := app/src/main/cpp
INC_DIR  := $(SRC_DIR)/include
JAVA_DIR := app/src/main/java/ar/sg
RES_DIR  := app/src/main/res
MANIFEST := app/src/main/AndroidManifest.xml
OBJ_DIR  := $(BUILD)/obj
LIB_DIR  := $(BUILD)/lib/arm64-v8a
HOST_DIR := $(BUILD)/host
GEN_DIR  := $(BUILD)/gen
CLS_DIR  := $(BUILD)/classes
DEX_DIR  := $(BUILD)/dex
APK      := $(BUILD)/sg.apk
SO       := $(LIB_DIR)/libsg.so
RES_ZIP  := $(BUILD)/res.zip
BASE_APK := $(BUILD)/base.apk
GEN_R    := $(GEN_DIR)/ar/sg/R.java
CLS_STAMP := $(BUILD)/classes.stamp
DEX      := $(DEX_DIR)/classes.dex
UNALIGNED := $(BUILD)/unaligned.apk
ALIGNED  := $(BUILD)/aligned.apk
KEYSTORE := keystore/debug.keystore

# Sources. Core files are the ones the host tests link; sg_jni.c is device-only.
CORE_SRC  := $(addprefix $(SRC_DIR)/,sg_time.c sg_state.c sg_sleeplog.c sg_store.c sg_core.c)
DEV_SRC   := $(CORE_SRC) $(SRC_DIR)/sg_jni.c
DEV_OBJS  := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(DEV_SRC))
TEST_SRC  := $(wildcard tests/*.c)
TEST_HDRS := $(wildcard tests/*.h)
INC_HDRS  := $(wildcard $(INC_DIR)/*.h)
JAVA_SRC  := $(addprefix $(JAVA_DIR)/,$(addsuffix .java,Native Sg SystemReceiver AlarmReceiver ActionReceiver MainActivity))
RES_FILES := $(shell find $(RES_DIR) -type f 2>/dev/null)

# Device flags: exactly the S6 set from docs/ADVICE-architecture.md section 6.
CFLAGS_DEV = -std=c11 -O2 -flto=thin -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections \
  -march=armv9.2-a -mtune=cortex-a720 -mbranch-protection=standard \
  -fstack-protector-strong -fstack-clash-protection -ftrivial-auto-var-init=zero \
  -D_FORTIFY_SOURCE=3 -DNDEBUG -Wall -Wextra -Werror -Wformat=2 -Wconversion -Wshadow \
  -Wvla -Wimplicit-fallthrough -I$(INC_DIR)
LDFLAGS_DEV = -shared -flto=thin -Wl,-z,relro,-z,now,-z,noexecstack,-z,max-page-size=16384 \
  -Wl,--gc-sections,--build-id=sha1,--no-undefined -Wl,-soname,libsg.so -s -llog

# Host flags (tests, valgrind, lint).
HOST_CFLAGS = -std=c11 -O1 -g -Wall -Wextra -Werror -Wformat=2 -Wconversion -Wshadow -Wvla \
  -fstack-protector-strong -D_FORTIFY_SOURCE=2 -I$(INC_DIR)
ASAN_FLAGS  = -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
TIDY_CHECKS = bugprone-*,cert-*,clang-analyzer-*,misc-*,-misc-no-recursion,readability-*

.PHONY: apk test asan valgrind lint clean

# Clear errors (not "No rule to make target") while parallel cards are still writing sources.
ifneq ($(filter apk lint,$(or $(MAKECMDGOALS),apk)),)
MISSING_APK := $(strip $(foreach f,$(DEV_SRC) $(JAVA_SRC) $(MANIFEST) $(RES_DIR)/values/strings.xml,$(if $(wildcard $f),,$f)))
ifneq ($(MISSING_APK),)
$(error make apk/lint: missing source(s): $(MISSING_APK) (written by another operator card; not yet present))
endif
endif
ifneq ($(filter test asan valgrind,$(MAKECMDGOALS)),)
MISSING_TEST := $(strip $(foreach f,$(CORE_SRC),$(if $(wildcard $f),,$f)))
ifneq ($(MISSING_TEST),)
$(error make test: missing source(s): $(MISSING_TEST) (written by another operator card; not yet present))
endif
ifeq ($(TEST_SRC),)
$(error make test: no tests/*.c found)
endif
endif

apk: $(APK)

# ---- device library -------------------------------------------------------
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(INC_HDRS)
	@mkdir -p $(@D)
	$(NDK_CLANG) $(CFLAGS_DEV) -c $< -o $@

$(SO): $(DEV_OBJS)
	@mkdir -p $(@D)
	$(NDK_CLANG) $(CFLAGS_DEV) $(LDFLAGS_DEV) -o $@ $(DEV_OBJS)

# ---- resources and Java -----------------------------------------------------
$(RES_ZIP): $(RES_FILES)
	@mkdir -p $(BUILD)
	$(BUILD_TOOLS)/aapt2 compile --dir $(RES_DIR) -o $@

$(BASE_APK): $(MANIFEST) $(RES_ZIP)
	@mkdir -p $(GEN_DIR)
	$(BUILD_TOOLS)/aapt2 link -o $@ --manifest $(MANIFEST) -I $(ANDROID_JAR) \
	  --min-sdk-version 35 --target-sdk-version 36 --java $(GEN_DIR) $(RES_ZIP)

$(CLS_STAMP): $(BASE_APK) $(JAVA_SRC)
	rm -rf $(CLS_DIR) && mkdir -p $(CLS_DIR)
	javac -source 8 -target 8 -Xlint:-options -bootclasspath $(ANDROID_JAR) -d $(CLS_DIR) \
	  $(GEN_R) $(JAVA_SRC)
	touch $@

$(DEX): $(CLS_STAMP)
	mkdir -p $(DEX_DIR)
	$(BUILD_TOOLS)/d8 --release --min-api 35 --lib $(ANDROID_JAR) --output $(DEX_DIR) \
	  $$(find $(CLS_DIR) -name '*.class')

# ---- packaging --------------------------------------------------------------
# .so stored uncompressed (zip -0) so extractNativeLibs=false can mmap it; zipalign -P 16 pads it to 16 KB.
$(UNALIGNED): $(BASE_APK) $(DEX) $(SO)
	cp $(BASE_APK) $@
	cd $(BUILD) && zip -q -j unaligned.apk dex/classes.dex && zip -q -0 unaligned.apk lib/arm64-v8a/libsg.so

$(ALIGNED): $(UNALIGNED)
	$(BUILD_TOOLS)/zipalign -P 16 -f 4 $< $@
	$(BUILD_TOOLS)/zipalign -c -P 16 4 $@

$(KEYSTORE):
	mkdir -p $(@D)
	keytool -genkeypair -keystore $@ -storepass android -keypass android -alias sgdebug \
	  -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=SG Debug"

$(APK): $(ALIGNED) $(KEYSTORE)
	$(BUILD_TOOLS)/apksigner sign --ks $(KEYSTORE) --ks-pass pass:android --ks-key-alias sgdebug \
	  --v2-signing-enabled true --v3-signing-enabled true --out $@ $(ALIGNED)
	$(BUILD_TOOLS)/apksigner verify --print-certs $@

# ---- host tests -------------------------------------------------------------
$(HOST_DIR)/sg_test_asan: $(CORE_SRC) $(TEST_SRC) $(INC_HDRS) $(TEST_HDRS)
	@mkdir -p $(HOST_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(ASAN_FLAGS) -Itests -o $@ $(CORE_SRC) $(TEST_SRC)

$(HOST_DIR)/sg_test_plain: $(CORE_SRC) $(TEST_SRC) $(INC_HDRS) $(TEST_HDRS)
	@mkdir -p $(HOST_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -Itests -o $@ $(CORE_SRC) $(TEST_SRC)

test: $(HOST_DIR)/sg_test_asan
	@for tz in $(TEST_TZS); do \
	  echo "== TZ=$$tz"; TZ=$$tz $(HOST_DIR)/sg_test_asan || exit 1; \
	done

asan: test

valgrind: $(HOST_DIR)/sg_test_plain
	TZ=$(firstword $(TEST_TZS)) valgrind --leak-check=full --show-leak-kinds=all \
	  --errors-for-leak-kinds=all --error-exitcode=1 $(HOST_DIR)/sg_test_plain

# ---- lint -------------------------------------------------------------------
lint: $(APK)
	@set -e; \
	if ! command -v clang-tidy >/dev/null 2>&1; then echo "make lint: clang-tidy not installed" >&2; exit 1; fi; \
	mkdir -p $(BUILD)/lint-inc; \
	ln -sf $(NDK_SYSROOT)/usr/include/jni.h $(BUILD)/lint-inc/jni.h; \
	for f in $(DEV_SRC); do \
	  clang-tidy --quiet --checks='$(TIDY_CHECKS)' --warnings-as-errors='*' $$f -- \
	    -std=c11 -I$(INC_DIR) -I$(BUILD)/lint-inc || exit 1; \
	done
	@if command -v cppcheck >/dev/null 2>&1; then \
	  cppcheck --quiet --error-exitcode=1 --std=c11 --enable=warning,performance,portability -I$(INC_DIR) $(DEV_SRC); \
	else echo "note: cppcheck not installed, skipped"; fi
	@if grep -rEn '\b(malloc|calloc|realloc|strdup|strcpy|strcat|sprintf|alloca)\s*\(' $(SRC_DIR); then \
	  echo "make lint: banned libc call found (see above)" >&2; exit 1; fi
	tools/lint-apk.sh $(APK)

clean:
	rm -rf $(BUILD)
