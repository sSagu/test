/* sg_store.c - persistence of SgState (format and contract: sg_store.h). Card C2.
 * All integers are stored little-endian byte by byte. No heap, no printf-family
 * beyond bounded snprintf.
 */
#define _DEFAULT_SOURCE 1
#define _POSIX_C_SOURCE 200809L

#include "sg_store.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sg_state.h"

/* Layout constants (offsets from sg_store.h table). */
#define OFF_MAGIC          0
#define OFF_VERSION        4
#define OFF_TOTAL_LEN      6
#define OFF_CRC            8
#define OFF_RESERVED       12
#define OFF_FLAGS          16
#define OFF_SNOOZE         17
#define OFF_LEAD           18
#define OFF_TARGET         20
#define OFF_WINDDOWN       22
#define OFF_LAST_SEEN_T    24
#define OFF_LAST_F1_T      32
#define OFF_LAST_F2_T      40
#define OFF_LAST_F5_T      48
#define OFF_LAST_NOTIF     56
#define OFF_DEBOUNCE_DUE   64
#define OFF_DEBOUNCE_T     72
#define OFF_LAST_F5_DATE   80
#define OFF_NIGHT_COUNT    84
#define OFF_NIGHT_HEAD     86
#define OFF_REF_COUNT      88
#define OFF_REF_HEAD       89
#define OFF_NIGHTS         96
#define NIGHT_SIZE         24
#define OFF_REF_MOD        2256
#define CRC_START          16

_Static_assert(SG_LOG_RETAIN_NIGHTS == 90, "file format v1 assumes 90 nights");
_Static_assert(SG_REF_SAMPLES_MAX == 10, "file format v1 assumes 10 ref samples");
_Static_assert(OFF_NIGHTS + SG_LOG_RETAIN_NIGHTS * NIGHT_SIZE == OFF_REF_MOD,
               "nights block must end at ref_mod");
_Static_assert(OFF_REF_MOD + SG_REF_SAMPLES_MAX * 2 + 28 == SG_STORE_SIZE,
               "ref_mod plus pad must end at SG_STORE_SIZE");

/* ---- little-endian helpers ---- */

static inline void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static inline void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static inline void put_u64(uint8_t *p, uint64_t v)
{
    size_t i;
    for (i = 0; i < 8; i++) {
        p[i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
    }
}

static inline uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

static inline uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t get_u64(const uint8_t *p)
{
    uint64_t v = 0;
    size_t i;
    for (i = 8; i > 0; i--) {
        v = (v << 8) | (uint64_t)p[i - 1];
    }
    return v;
}

static inline void put_i16(uint8_t *p, int16_t v) { put_u16(p, (uint16_t)v); }
static inline void put_i32(uint8_t *p, int32_t v) { put_u32(p, (uint32_t)v); }
static inline void put_i64(uint8_t *p, int64_t v) { put_u64(p, (uint64_t)v); }
static inline int16_t get_i16(const uint8_t *p) { return (int16_t)get_u16(p); }
static inline int32_t get_i32(const uint8_t *p) { return (int32_t)get_u32(p); }
static inline int64_t get_i64(const uint8_t *p) { return (int64_t)get_u64(p); }

/* ---- CRC32 (IEEE, bitwise) ---- */

uint32_t sg_store_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    unsigned k;
    for (i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (k = 0; k < 8; k++) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ---- encode / decode ---- */

void sg_store_encode(const SgState *s, uint8_t buf[SG_STORE_SIZE])
{
    size_t i;
    size_t off;

    memset(buf, 0, SG_STORE_SIZE);

    put_u32(buf + OFF_MAGIC, SG_STORE_MAGIC);
    put_u16(buf + OFF_VERSION, SG_STORE_VERSION);
    put_u16(buf + OFF_TOTAL_LEN, SG_STORE_SIZE);
    /* CRC at OFF_CRC written last; reserved stays 0. */

    buf[OFF_FLAGS] = s->flags;
    buf[OFF_SNOOZE] = s->snooze_count;
    put_u16(buf + OFF_LEAD, s->lead_min);
    put_u16(buf + OFF_TARGET, s->target_sleep_min);
    put_u16(buf + OFF_WINDDOWN, s->winddown_min);
    put_i64(buf + OFF_LAST_SEEN_T, s->last_seen_T);
    put_i64(buf + OFF_LAST_F1_T, s->last_f1_for_T);
    put_i64(buf + OFF_LAST_F2_T, s->last_f2_for_T);
    put_i64(buf + OFF_LAST_F5_T, s->last_f5_for_T);
    put_i64(buf + OFF_LAST_NOTIF, s->last_notified_ms);
    put_i64(buf + OFF_DEBOUNCE_DUE, s->debounce_due_ms);
    put_i64(buf + OFF_DEBOUNCE_T, s->debounce_T);
    put_i32(buf + OFF_LAST_F5_DATE, s->last_f5_date);
    put_u16(buf + OFF_NIGHT_COUNT, s->night_count);
    put_u16(buf + OFF_NIGHT_HEAD, s->night_head);
    buf[OFF_REF_COUNT] = s->ref_count;
    buf[OFF_REF_HEAD] = s->ref_head;

    for (i = 0; i < SG_LOG_RETAIN_NIGHTS; i++) {
        off = OFF_NIGHTS + i * NIGHT_SIZE;
        put_i64(buf + off, s->nights[i].bed_ms);
        put_i64(buf + off + 8, s->nights[i].wake_ms);
        buf[off + 16] = s->nights[i].closed;
        /* bytes off+17 .. off+23 stay zero */
    }

    for (i = 0; i < SG_REF_SAMPLES_MAX; i++) {
        put_i16(buf + OFF_REF_MOD + i * 2, s->ref_mod[i]);
    }
    /* final 28 pad bytes stay zero */

    put_u32(buf + OFF_CRC, sg_store_crc32(buf + CRC_START, SG_STORE_SIZE - CRC_START));
}

int sg_store_decode(const uint8_t buf[SG_STORE_SIZE], SgState *s)
{
    size_t i;
    size_t off;

    if (get_u32(buf + OFF_MAGIC) != SG_STORE_MAGIC ||
        get_u16(buf + OFF_VERSION) != SG_STORE_VERSION ||
        get_u16(buf + OFF_TOTAL_LEN) != SG_STORE_SIZE ||
        get_u32(buf + OFF_CRC) !=
            sg_store_crc32(buf + CRC_START, SG_STORE_SIZE - CRC_START)) {
        sg_state_defaults(s);
        return SG_STORE_E_CORRUPT;
    }

    s->flags = buf[OFF_FLAGS];
    s->snooze_count = buf[OFF_SNOOZE];
    s->lead_min = get_u16(buf + OFF_LEAD);
    s->target_sleep_min = get_u16(buf + OFF_TARGET);
    s->winddown_min = get_u16(buf + OFF_WINDDOWN);
    s->last_seen_T = get_i64(buf + OFF_LAST_SEEN_T);
    s->last_f1_for_T = get_i64(buf + OFF_LAST_F1_T);
    s->last_f2_for_T = get_i64(buf + OFF_LAST_F2_T);
    s->last_f5_for_T = get_i64(buf + OFF_LAST_F5_T);
    s->last_notified_ms = get_i64(buf + OFF_LAST_NOTIF);
    s->debounce_due_ms = get_i64(buf + OFF_DEBOUNCE_DUE);
    s->debounce_T = get_i64(buf + OFF_DEBOUNCE_T);
    s->last_f5_date = get_i32(buf + OFF_LAST_F5_DATE);
    s->night_count = get_u16(buf + OFF_NIGHT_COUNT);
    s->night_head = get_u16(buf + OFF_NIGHT_HEAD);
    s->ref_count = buf[OFF_REF_COUNT];
    s->ref_head = buf[OFF_REF_HEAD];

    for (i = 0; i < SG_LOG_RETAIN_NIGHTS; i++) {
        off = OFF_NIGHTS + i * NIGHT_SIZE;
        s->nights[i].bed_ms = get_i64(buf + off);
        s->nights[i].wake_ms = get_i64(buf + off + 8);
        s->nights[i].closed = buf[off + 16];
    }

    for (i = 0; i < SG_REF_SAMPLES_MAX; i++) {
        s->ref_mod[i] = get_i16(buf + OFF_REF_MOD + i * 2);
    }

    (void)sg_state_sanitize(s);
    return SG_STORE_OK;
}

/* ---- file helpers ---- */

/* Returns 1 if path is non-empty and strlen(path) < SG_STORE_PATH_MAX. */
static int path_fits(const char *path)
{
    size_t i;
    for (i = 0; i < SG_STORE_PATH_MAX; i++) {
        if (path[i] == '\0') {
            return i > 0;
        }
    }
    return 0;
}

/* Best-effort rename of a corrupt file to "<path>.bad". */
static void quarantine(const char *path)
{
    char bad[SG_STORE_PATH_MAX + 8];
    int n = snprintf(bad, sizeof bad, "%s.bad", path);
    if (n > 0 && (size_t)n < sizeof bad) {
        (void)rename(path, bad);
    }
}

/* Read exactly SG_STORE_SIZE bytes from fd. Returns bytes read, or -1 on I/O error. */
static int read_full(int fd, uint8_t *buf)
{
    size_t got = 0;
    while (got < SG_STORE_SIZE) {
        ssize_t r = read(fd, buf + got, SG_STORE_SIZE - got);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (r == 0) {
            break;
        }
        got += (size_t)r;
    }
    return (int)got;
}

/* Write all SG_STORE_SIZE bytes to fd. Returns 0 on success, -1 on error. */
static int write_full(int fd, const uint8_t *buf)
{
    size_t off = 0;
    while (off < SG_STORE_SIZE) {
        ssize_t w = write(fd, buf + off, SG_STORE_SIZE - off);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (w == 0) {
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

/* fsync with EINTR retry. */
static int fsync_full(int fd)
{
    while (fsync(fd) != 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return 0;
}

/* ---- load / save ---- */

int sg_store_load(const char *path, SgState *s)
{
    uint8_t buf[SG_STORE_SIZE];
    struct stat st;
    int fd;
    int got;

    if (s == NULL) {
        return SG_STORE_E_ARG;
    }
    if (path == NULL || !path_fits(path)) {
        sg_state_defaults(s);
        return SG_STORE_E_ARG;
    }

    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        sg_state_defaults(s);
        return (errno == ENOENT) ? SG_STORE_E_NOFILE : SG_STORE_E_IO;
    }

    if (fstat(fd, &st) != 0) {
        (void)close(fd);
        sg_state_defaults(s);
        return SG_STORE_E_IO;
    }
    if (!S_ISREG(st.st_mode) || st.st_size != (off_t)SG_STORE_SIZE) {
        (void)close(fd);
        sg_state_defaults(s);
        quarantine(path);
        return SG_STORE_E_CORRUPT;
    }

    got = read_full(fd, buf);
    (void)close(fd);
    if (got < 0) {
        sg_state_defaults(s);
        return SG_STORE_E_IO;
    }
    if (got != (int)SG_STORE_SIZE) {
        sg_state_defaults(s);
        quarantine(path);
        return SG_STORE_E_CORRUPT;
    }

    if (sg_store_decode(buf, s) != SG_STORE_OK) {
        quarantine(path);
        return SG_STORE_E_CORRUPT;
    }
    return SG_STORE_OK;
}

/* Parent directory of path into dir (size SG_STORE_PATH_MAX). */
static int parent_dir(const char *path, char *dir, size_t cap)
{
    size_t len = strlen(path);
    size_t slash = len;
    size_t i;

    for (i = len; i > 0; i--) {
        if (path[i - 1] == '/') {
            slash = i - 1;
            break;
        }
    }
    if (slash == len) {
        if (cap < 2) {
            return -1;
        }
        dir[0] = '.';
        dir[1] = '\0';
        return 0;
    }
    if (slash == 0) {
        slash = 1; /* root directory "/" */
    }
    if (slash + 1 > cap) {
        return -1;
    }
    memcpy(dir, path, slash);
    dir[slash] = '\0';
    return 0;
}

int sg_store_save(const char *path, const SgState *s)
{
    uint8_t buf[SG_STORE_SIZE];
    char tmp[SG_STORE_PATH_MAX + 8];
    char dir[SG_STORE_PATH_MAX + 8];
    int n;
    int fd;
    int ok = 0;
    int dfd;

    if (path == NULL || s == NULL || !path_fits(path)) {
        return SG_STORE_E_ARG;
    }
    n = snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof tmp) {
        return SG_STORE_E_ARG;
    }
    if (parent_dir(path, dir, sizeof dir) != 0) {
        return SG_STORE_E_ARG;
    }

    sg_store_encode(s, buf);

    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        return SG_STORE_E_IO;
    }
    if (fchmod(fd, 0600) == 0 && write_full(fd, buf) == 0 &&
        fsync_full(fd) == 0 && close(fd) == 0) {
        ok = 1;
    } else {
        (void)close(fd);
    }
    if (!ok) {
        (void)unlink(tmp);
        return SG_STORE_E_IO;
    }

    if (rename(tmp, path) != 0) {
        (void)unlink(tmp);
        return SG_STORE_E_IO;
    }

    dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0) {
        return SG_STORE_E_IO;
    }
    if (fsync(dfd) != 0 && errno != EINVAL) {
        (void)close(dfd);
        return SG_STORE_E_IO;
    }
    (void)close(dfd);
    return SG_STORE_OK;
}
