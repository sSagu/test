/* test_store.c - sg_store.h: format layout, CRC, atomic save/load, corruption handling.
 * Every test works in a mkdtemp() dir under /tmp and removes everything it created. */
#include "sg_test.h"
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    char dir[64];
    char file[160];
    char tmp[168];
    char bad[168];
} Paths;

static int paths_new(Paths *p) {
    char tmpl[64];
    (void)snprintf(tmpl, sizeof tmpl, "/tmp/sg_t1_XXXXXX");
    if (mkdtemp(tmpl) == NULL) {
        return -1;
    }
    (void)snprintf(p->dir, sizeof p->dir, "%s", tmpl);
    (void)snprintf(p->file, sizeof p->file, "%s/sg_state.bin", p->dir);
    (void)snprintf(p->tmp, sizeof p->tmp, "%s.tmp", p->file);
    (void)snprintf(p->bad, sizeof p->bad, "%s.bad", p->file);
    return 0;
}

static void paths_free(const Paths *p) {
    (void)unlink(p->file);
    (void)unlink(p->tmp);
    (void)unlink(p->bad);
    (void)rmdir(p->dir);
}

static int path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int write_raw(const char *path, const uint8_t *buf, size_t n) {
    FILE *f = fopen(path, "wb");
    size_t w;
    int rc;
    if (f == NULL) {
        return -1;
    }
    w = fwrite(buf, 1, n, f);
    rc = fclose(f);
    return (w == n && rc == 0) ? 0 : -1;
}

static void put16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* A valid, non-default state with every persisted field exercised. */
static void fill_state(SgState *s) {
    sg_state_defaults(s);
    s->flags = (uint8_t)(SG_FLAG_ENABLED | SG_FLAG_WINDDOWN_ON | SG_FLAG_NOTIF_PROMPTED);
    s->snooze_count = 1;
    s->lead_min = 600;
    s->target_sleep_min = 450;
    s->winddown_min = 45;
    s->last_seen_T = INT64_C(1790000000000);
    s->last_f1_for_T = INT64_C(1790000100000);
    s->last_f2_for_T = INT64_C(1790000200000);
    s->last_f5_for_T = INT64_C(1790000300000);
    s->last_notified_ms = INT64_C(1790000400000);
    s->debounce_due_ms = INT64_C(1790000500000);
    s->debounce_T = INT64_C(1790000600000);
    s->last_f5_date = 20261009;
    s->night_count = 2;
    s->night_head = 3;
    s->nights[3].bed_ms = INT64_C(1789990000000);
    s->nights[3].wake_ms = INT64_C(1790020000000);
    s->nights[3].closed = 1;
    s->nights[4].bed_ms = INT64_C(1790050000000);
    s->nights[4].wake_ms = 0;
    s->nights[4].closed = 0;
    s->ref_count = 3;
    s->ref_head = 2;
    s->ref_mod[0] = 420;
    s->ref_mod[1] = 430;
    s->ref_mod[2] = 440;
}

static int same_image(const SgState *a, const SgState *b) {
    uint8_t ea[SG_STORE_SIZE], eb[SG_STORE_SIZE];
    sg_store_encode(a, ea);
    sg_store_encode(b, eb);
    return memcmp(ea, eb, SG_STORE_SIZE) == 0;
}

static void test_store_crc32_known(void) {
    SG_CHECK(sg_store_crc32((const uint8_t *)"123456789", 9) == UINT32_C(0xCBF43926));
    SG_CHECK(sg_store_crc32((const uint8_t *)"", 0) == 0);
}

static void test_store_layout_bytes(void) {
    SgState s;
    uint8_t buf[SG_STORE_SIZE];
    uint32_t crc;
    size_t i;
    sg_state_defaults(&s);
    sg_store_encode(&s, buf);
    SG_CHECK(buf[0] == 'S' && buf[1] == 'G' && buf[2] == 'S' && buf[3] == '1');
    SG_CHECK(buf[4] == 1 && buf[5] == 0);
    SG_CHECK(buf[6] == 0x00 && buf[7] == 0x09);             /* total_len 2304 */
    crc = sg_store_crc32(buf + 16, SG_STORE_SIZE - 16);
    SG_CHECK(get32(buf + 8) == crc);
    SG_CHECK(buf[12] == 0 && buf[13] == 0 && buf[14] == 0 && buf[15] == 0);
    SG_CHECK(buf[16] == s.flags);
    SG_CHECK(buf[18] == 0x3A && buf[19] == 0x02);          /* lead 570 LE */
    for (i = 90; i < 96; i++) {
        SG_CHECK(buf[i] == 0);
    }
    for (i = 2276; i < SG_STORE_SIZE; i++) {
        SG_CHECK(buf[i] == 0);
    }
}

static void test_store_roundtrip_encode(void) {
    SgState a, b;
    uint8_t e1[SG_STORE_SIZE];
    fill_state(&a);
    sg_state_defaults(&b);
    sg_store_encode(&a, e1);
    SG_CHECK(sg_store_decode(e1, &b) == SG_STORE_OK);
    SG_CHECK(same_image(&a, &b));
    SG_CHECK(b.lead_min == 600 && b.night_count == 2);
    SG_CHECK(b.nights[4].bed_ms == a.nights[4].bed_ms);
    SG_CHECK(sg_state_sanitize(&b) == 0);
}

static void test_store_save_load_file(void) {
    Paths p;
    SgState a, b;
    struct stat st;
    SG_CHECK(paths_new(&p) == 0);
    fill_state(&a);
    SG_CHECK(sg_store_save(p.file, &a) == SG_STORE_OK);
    SG_CHECK(!path_exists(p.tmp));
    SG_CHECK(stat(p.file, &st) == 0 && st.st_size == SG_STORE_SIZE);
    SG_CHECK((st.st_mode & 0777) == 0600);
    sg_state_defaults(&b);
    SG_CHECK(sg_store_load(p.file, &b) == SG_STORE_OK);
    SG_CHECK(same_image(&a, &b));
    /* overwrite with a different state */
    a.lead_min = 540;
    SG_CHECK(sg_store_save(p.file, &a) == SG_STORE_OK);
    SG_CHECK(sg_store_load(p.file, &b) == SG_STORE_OK);
    SG_CHECK(b.lead_min == 540);
    SG_CHECK(!path_exists(p.tmp));
    paths_free(&p);
}

static void test_store_missing_file(void) {
    Paths p;
    SgState s;
    SG_CHECK(paths_new(&p) == 0);
    fill_state(&s);
    SG_CHECK(sg_store_load(p.file, &s) == SG_STORE_E_NOFILE);
    SG_CHECK(s.lead_min == SG_LEAD_DEFAULT_MIN && s.night_count == 0);
    SG_CHECK(!path_exists(p.bad));
    paths_free(&p);
}

static void test_store_truncated_bad(void) {
    Paths p;
    SgState a, b;
    uint8_t buf[SG_STORE_SIZE];
    SG_CHECK(paths_new(&p) == 0);
    fill_state(&a);
    sg_store_encode(&a, buf);
    SG_CHECK(write_raw(p.file, buf, SG_STORE_SIZE - 1) == 0);
    fill_state(&b);
    SG_CHECK(sg_store_load(p.file, &b) == SG_STORE_E_CORRUPT);
    SG_CHECK(b.lead_min == SG_LEAD_DEFAULT_MIN && b.night_count == 0);
    SG_CHECK(!path_exists(p.file) && path_exists(p.bad));
    paths_free(&p);
}

static void test_store_flipped_byte_bad(void) {
    Paths p;
    SgState a, b;
    uint8_t buf[SG_STORE_SIZE];
    SG_CHECK(paths_new(&p) == 0);
    fill_state(&a);
    sg_store_encode(&a, buf);
    buf[1000] ^= 0x40;                  /* inside the CRC-protected region */
    SG_CHECK(write_raw(p.file, buf, SG_STORE_SIZE) == 0);
    fill_state(&b);
    SG_CHECK(sg_store_load(p.file, &b) == SG_STORE_E_CORRUPT);
    SG_CHECK(b.lead_min == SG_LEAD_DEFAULT_MIN);
    SG_CHECK(path_exists(p.bad));
    paths_free(&p);
}

static void test_store_bad_magic_version(void) {
    Paths p;
    SgState a, b;
    uint8_t buf[SG_STORE_SIZE];
    int k;
    for (k = 0; k < 3; k++) {
        SG_CHECK(paths_new(&p) == 0);
        fill_state(&a);
        sg_store_encode(&a, buf);
        if (k == 0) {
            buf[0] = 'X';                    /* wrong magic */
        } else if (k == 1) {
            put16(buf + 4, 2);               /* wrong version (CRC does not cover it) */
        } else {
            put16(buf + 6, 2300);            /* wrong total_len */
        }
        SG_CHECK(write_raw(p.file, buf, SG_STORE_SIZE) == 0);
        sg_state_defaults(&b);
        SG_CHECK(sg_store_load(p.file, &b) == SG_STORE_E_CORRUPT);
        SG_CHECK(path_exists(p.bad));
        paths_free(&p);
    }
}

static void test_store_sanitize_on_decode(void) {
    SgState s;
    uint8_t buf[SG_STORE_SIZE];
    uint32_t crc;
    sg_state_defaults(&s);
    sg_store_encode(&s, buf);
    put16(buf + 18, 9999);      /* lead_min out of range -> 660 */
    put16(buf + 84, 200);       /* night_count > 90 -> empty */
    crc = sg_store_crc32(buf + 16, SG_STORE_SIZE - 16);
    put32(buf + 8, crc);        /* keep the CRC valid so only sanitize can fix it */
    SG_CHECK(sg_store_decode(buf, &s) == SG_STORE_OK);
    SG_CHECK(s.lead_min == SG_LEAD_MAX_MIN);
    SG_CHECK(s.night_count == 0);
}

static void test_store_path_arg(void) {
    SgState s;
    char longp[600];
    sg_state_defaults(&s);
    memset(longp, 'a', sizeof longp - 1);
    longp[sizeof longp - 1] = '\0';
    SG_CHECK(sg_store_load(longp, &s) == SG_STORE_E_ARG);
    SG_CHECK(sg_store_save(longp, &s) == SG_STORE_E_ARG);
    SG_CHECK(sg_store_load(NULL, &s) == SG_STORE_E_ARG);
    SG_CHECK(sg_store_save(NULL, &s) == SG_STORE_E_ARG);
}

static void test_store_save_io_error(void) {
    SgState s;
    const char *path = "/tmp/sg_t1_no_such_dir_xyz/sg_state.bin";
    fill_state(&s);
    SG_CHECK(sg_store_save(path, &s) == SG_STORE_E_IO);
    SG_CHECK(!path_exists("/tmp/sg_t1_no_such_dir_xyz/sg_state.bin.tmp"));
}

static void test_store_no_fd_leak(void) {
    Paths p;
    SgState s;
    uint8_t buf[SG_STORE_SIZE];
    int before, after;
    if (paths_new(&p) != 0) {
        SG_CHECK(0);
        return;
    }
    fill_state(&s);
    before = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (before >= 0) {
        (void)close(before);
    }
    SG_CHECK(sg_store_save(p.file, &s) == SG_STORE_OK);
    SG_CHECK(sg_store_load(p.file, &s) == SG_STORE_OK);
    sg_store_encode(&s, buf);
    SG_CHECK(write_raw(p.file, buf, 100) == 0);
    SG_CHECK(sg_store_load(p.file, &s) == SG_STORE_E_CORRUPT);
    SG_CHECK(sg_store_save("/tmp/sg_t1_no_such_dir_xyz/sg_state.bin", &s) == SG_STORE_E_IO);
    SG_CHECK(sg_store_load(p.file, &s) == SG_STORE_E_NOFILE);
    after = open("/dev/null", O_RDONLY | O_CLOEXEC);
    SG_CHECK(after == before);
    if (after >= 0) {
        (void)close(after);
    }
    paths_free(&p);
}

void run_store_tests(void) {
    SG_RUN(test_store_crc32_known);
    SG_RUN(test_store_layout_bytes);
    SG_RUN(test_store_roundtrip_encode);
    SG_RUN(test_store_save_load_file);
    SG_RUN(test_store_missing_file);
    SG_RUN(test_store_truncated_bad);
    SG_RUN(test_store_flipped_byte_bad);
    SG_RUN(test_store_bad_magic_version);
    SG_RUN(test_store_sanitize_on_decode);
    SG_RUN(test_store_path_arg);
    SG_RUN(test_store_save_io_error);
    SG_RUN(test_store_no_fd_leak);
}
