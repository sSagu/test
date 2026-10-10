/* sg_jni.c - JNI glue for ar.sg.Native (ADVICE-architecture §4).
 * One static SgState guarded by one mutex. Every mutating call: lock, work on a
 * copy, adopt the copy, persist if the encoded image changed, flatten commands, unlock,
 * then build the Java array from a stack buffer. No C->Java calls, no global refs, no cached
 * JNIEnv. Only JNI_OnLoad / JNI_OnUnload are exported.
 */
#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "sg_cmd.h"
#include "sg_core.h"
#include "sg_state.h"
#include "sg_store.h"

#define SG_JNI_CLASS "ar/sg/Native"
#define SG_JNI_METHOD_COUNT 8

/* to_java_array stack buffer: large enough for a full command list AND the UI model
 * (SG_UI_LEN grew past SG_MAX_CMDS * SG_CMD_WORDS in v3). */
#define SG_JNI_CMD_WORDS (SG_MAX_CMDS * SG_CMD_WORDS)
#define SG_JNI_BUF_WORDS (SG_JNI_CMD_WORDS > SG_UI_LEN ? SG_JNI_CMD_WORDS : SG_UI_LEN)

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static SgState g_state;          /* guarded by g_mu */
static int g_ready;              /* g_state holds defaults or loaded data; guarded by g_mu */
static int g_loaded;             /* nativeInit succeeded with a path; persistence on; guarded by g_mu */
static int g_init_code;          /* SG_STORE_* from the first load; guarded by g_mu */
static char g_path[SG_STORE_PATH_MAX]; /* guarded by g_mu */

typedef int (*MutFn)(SgState *s, const SgObs *o, int arg, int32_t arg2, SgCmdList *out);

static int mu_lock(void)
{
    return pthread_mutex_lock(&g_mu) == 0;
}

static void mu_unlock(void)
{
    (void)pthread_mutex_unlock(&g_mu);
}

/* Call with g_mu held. */
static void ensure_ready_locked(void)
{
    if (!g_ready) {
        sg_state_defaults(&g_state);
        g_ready = 1;
    }
}

static int state_changed(const SgState *a, const SgState *b)
{
    uint8_t ea[SG_STORE_SIZE];
    uint8_t eb[SG_STORE_SIZE];

    sg_store_encode(a, ea);
    sg_store_encode(b, eb);
    return memcmp(ea, eb, SG_STORE_SIZE) != 0;
}

static SgObs make_obs(jlong now, jlong next, jint creator, jint exact, jint notif)
{
    SgObs o;

    o.now_ms = (int64_t)now;
    o.next_alarm_ms = (int64_t)next;
    o.creator = (uint8_t)((creator >= SG_CREATOR_NONE && creator <= SG_CREATOR_OTHER)
                          ? creator : SG_CREATOR_NONE);
    o.exact_allowed = (uint8_t)(exact != 0 ? 1 : 0);
    o.notif_allowed = (uint8_t)(notif != 0 ? 1 : 0);
    return o;
}

/* Runs with no lock held. Returns NULL on allocation or JNI failure. */
static jlongArray to_java_array(JNIEnv *env, const int64_t *words, int32_t n)
{
    jlong buf[SG_JNI_BUF_WORDS];
    jlongArray arr;
    int32_t i;

    if (n < 0 || n > (int32_t)(sizeof buf / sizeof buf[0])) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        buf[i] = (jlong)words[i];
    }
    arr = (*env)->NewLongArray(env, (jsize)n);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    if (arr == NULL) {
        return NULL;
    }
    if (n > 0) {
        (*env)->SetLongArrayRegion(env, arr, 0, (jsize)n, buf);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            return NULL;
        }
    }
    return arr;
}

static int do_sync(SgState *s, const SgObs *o, int arg, int32_t arg2, SgCmdList *out)
{
    (void)arg2;
    return sg_core_sync(s, o, arg, out);
}

static int do_alarm(SgState *s, const SgObs *o, int arg, int32_t arg2, SgCmdList *out)
{
    (void)arg2;
    return sg_core_alarm_fired(s, o, arg, out);
}

static int do_action(SgState *s, const SgObs *o, int arg, int32_t arg2, SgCmdList *out)
{
    (void)arg2;
    return sg_core_action(s, o, arg, out);
}

static int do_set(SgState *s, const SgObs *o, int arg, int32_t arg2, SgCmdList *out)
{
    return sg_core_set(s, o, arg, arg2, out);
}

/* v3: arg = date (yyyymmdd), arg2 = minutes. */
static int do_log_night(SgState *s, const SgObs *o, int arg, int32_t arg2, SgCmdList *out)
{
    return sg_core_log_night(s, o, (int32_t)arg, arg2, out);
}

static jlongArray mutate(JNIEnv *env, MutFn fn, const SgObs *o, int arg, int32_t arg2)
{
    SgCmdList list;
    SgState work;
    int64_t words[SG_MAX_CMDS * SG_CMD_WORDS];
    int32_t n = 0;

    sg_cmd_init(&list);
    if (!mu_lock()) {
        return NULL;
    }
    ensure_ready_locked();
    work = g_state;
    if (fn(&work, o, arg, arg2, &list) == SG_OK) {
        int changed = state_changed(&g_state, &work);

        /* Adopt always: SgState also holds in-memory-only fields (boot_unseen,
         * ADVICE-v2 section 4) that the encoded image does not show. */
        g_state = work;
        if (changed && g_loaded) {
            (void)sg_store_save(g_path, &g_state);
        }
        n = sg_cmd_flatten(&list, words, (int32_t)(sizeof words / sizeof words[0]));
    }
    mu_unlock();
    return to_java_array(env, words, n);
}

static jint jni_init(JNIEnv *env, jclass cls, jstring path)
{
    char local[SG_STORE_PATH_MAX];
    const char *p;
    size_t len;
    int code;

    (void)cls;
    if (path == NULL) {
        return SG_STORE_E_ARG;
    }
    p = (*env)->GetStringUTFChars(env, path, NULL);
    if (p == NULL) {
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
        return SG_STORE_E_ARG;
    }
    len = strlen(p);
    if (len == 0 || len >= sizeof local) {
        (*env)->ReleaseStringUTFChars(env, path, p);
        return SG_STORE_E_ARG;
    }
    memcpy(local, p, len + 1);
    (*env)->ReleaseStringUTFChars(env, path, p);

    if (!mu_lock()) {
        return SG_STORE_E_IO;
    }
    if (g_loaded) {
        code = g_init_code;
    } else {
        memcpy(g_path, local, len + 1);
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
    }
    mu_unlock();
    return (jint)code;
}

static jlongArray jni_sync(JNIEnv *env, jclass cls, jlong now, jlong next, jint creator,
                           jint exact, jint notif, jint reason)
{
    SgObs o;

    (void)cls;
    o = make_obs(now, next, creator, exact, notif);
    return mutate(env, do_sync, &o, (int)reason, 0);
}

static jlongArray jni_alarm_fired(JNIEnv *env, jclass cls, jlong now, jlong next, jint creator,
                                  jint exact, jint notif, jint alarm_id)
{
    SgObs o;

    (void)cls;
    o = make_obs(now, next, creator, exact, notif);
    return mutate(env, do_alarm, &o, (int)alarm_id, 0);
}

static jlongArray jni_action(JNIEnv *env, jclass cls, jlong now, jlong next, jint creator,
                             jint exact, jint notif, jint action_id)
{
    SgObs o;

    (void)cls;
    o = make_obs(now, next, creator, exact, notif);
    return mutate(env, do_action, &o, (int)action_id, 0);
}

static jlongArray jni_set(JNIEnv *env, jclass cls, jlong now, jlong next, jint creator,
                          jint exact, jint notif, jint key, jint value)
{
    SgObs o;

    (void)cls;
    o = make_obs(now, next, creator, exact, notif);
    return mutate(env, do_set, &o, (int)key, (int32_t)value);
}

static jlongArray jni_log_night(JNIEnv *env, jclass cls, jlong now, jlong next, jint creator,
                                jint exact, jint notif, jint date, jint minutes)
{
    SgObs o;

    (void)cls;
    o = make_obs(now, next, creator, exact, notif);
    return mutate(env, do_log_night, &o, (int)date, (int32_t)minutes);
}

static jint jni_get(JNIEnv *env, jclass cls, jint key)
{
    int32_t v;

    (void)env;
    (void)cls;
    if (!mu_lock()) {
        return -1;
    }
    ensure_ready_locked();
    v = sg_core_get(&g_state, (int)key);
    mu_unlock();
    return (jint)v;
}

static jlongArray jni_ui_model(JNIEnv *env, jclass cls, jlong now, jlong next, jint creator,
                               jint exact, jint notif)
{
    SgUiModel model;
    SgObs o;
    int64_t words[SG_UI_LEN];
    int rc;

    (void)cls;
    o = make_obs(now, next, creator, exact, notif);
    if (!mu_lock()) {
        return NULL;
    }
    ensure_ready_locked();
    rc = sg_core_ui(&g_state, &o, &model);
    if (rc == SG_OK) {
        sg_core_ui_flatten(&model, words);
    }
    mu_unlock();
    if (rc != SG_OK) {
        return NULL;
    }
    return to_java_array(env, words, (int32_t)SG_UI_LEN);
}

static const JNINativeMethod g_methods[SG_JNI_METHOD_COUNT] = {
    { "nativeInit", "(Ljava/lang/String;)I", (void *)jni_init },
    { "nativeSync", "(JJIIII)[J", (void *)jni_sync },
    { "nativeAlarmFired", "(JJIIII)[J", (void *)jni_alarm_fired },
    { "nativeAction", "(JJIIII)[J", (void *)jni_action },
    { "nativeSet", "(JJIIIII)[J", (void *)jni_set },
    { "nativeGet", "(I)I", (void *)jni_get },
    { "nativeUiModel", "(JJIII)[J", (void *)jni_ui_model },
    { "nativeLogNight", "(JJIIIII)[J", (void *)jni_log_night }
};

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
    JNIEnv *env = NULL;
    jclass cls;
    jint rc;

    (void)reserved;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    cls = (*env)->FindClass(env, SG_JNI_CLASS);
    if (cls == NULL) {
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
        return JNI_ERR;
    }
    rc = (*env)->RegisterNatives(env, cls, g_methods, SG_JNI_METHOD_COUNT);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        rc = JNI_ERR;
    }
    (*env)->DeleteLocalRef(env, cls);
    if (rc != JNI_OK) {
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNI_OnUnload(JavaVM *vm, void *reserved)
{
    (void)vm;
    (void)reserved;
    (void)pthread_mutex_destroy(&g_mu);
}
