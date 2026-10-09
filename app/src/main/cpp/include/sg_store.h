/* sg_store.h - persistence of SgState as one fixed-size little-endian file.
 * Implementation: sg_store.c (card C2). Treat file contents as UNTRUSTED.
 *
 * FILE FORMAT v1 (SG_STORE_SIZE = 2304 bytes, all integers little-endian):
 *  off  size  field
 *  0    4     magic  = 0x31534753 ("SGS1" as bytes 'S','G','S','1')
 *  4    2     version = 1
 *  6    2     total_len = 2304
 *  8    4     crc32 (IEEE 802.3, poly 0xEDB88320, init 0xFFFFFFFF, final xor) of bytes [16, 2304)
 *  12   4     reserved = 0
 *  16   1     flags            (SG_FLAG_*)
 *  17   1     snooze_count
 *  18   2     lead_min
 *  20   2     target_sleep_min
 *  22   2     winddown_min
 *  24   8     last_seen_T
 *  32   8     last_f1_for_T
 *  40   8     last_f2_for_T
 *  48   8     last_f5_for_T
 *  56   8     last_notified_ms
 *  64   8     debounce_due_ms
 *  72   8     debounce_T
 *  80   4     last_f5_date
 *  84   2     night_count
 *  86   2     night_head
 *  88   1     ref_count
 *  89   1     ref_head
 *  90   6     pad = 0
 *  96   2160  nights[90], each 24 bytes: i64 bed_ms, i64 wake_ms, u8 closed, 7 pad
 *  2256 20    ref_mod[10], each i16
 *  2276 28    pad = 0
 *  2304 end
 *
 * Writing is atomic: write "<path>.tmp" (O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC, mode 0600),
 * fsync, close, rename(tmp, path), then fsync the parent directory. Reading: open
 * O_RDONLY|O_CLOEXEC|O_NOFOLLOW, fstat must be a regular file, read exactly
 * SG_STORE_SIZE bytes (short read = corrupt). Any failure -> SG_STORE_E_* and the
 * caller falls back to sg_state_defaults(). A corrupt file is renamed to
 * "<path>.bad" (best effort) so it is not re-parsed forever.
 * Paths: caller supplies the full file path (Context.getFilesDir() + "/sg_state.bin");
 * path length must be < SG_STORE_PATH_MAX including NUL.
 */
#ifndef SG_STORE_H
#define SG_STORE_H

#include <stddef.h>
#include <stdint.h>
#include "sg_state.h"

#define SG_STORE_MAGIC     UINT32_C(0x31534753)
#define SG_STORE_VERSION   1
#define SG_STORE_SIZE      2304
#define SG_STORE_PATH_MAX  512   /* strlen(path) + strlen(".tmp") must fit */

enum {
    SG_STORE_OK          = 0,
    SG_STORE_E_NOFILE    = 1,   /* file absent: not an error for first run */
    SG_STORE_E_IO        = 2,   /* open/read/write/fsync/rename failed (errno lost) */
    SG_STORE_E_CORRUPT   = 3,   /* bad magic/version/len/crc or short read */
    SG_STORE_E_ARG       = 4    /* NULL or path too long */
};

/* Serialise s into buf (exactly SG_STORE_SIZE bytes, caller-provided). Computes crc. */
void sg_store_encode(const SgState *s, uint8_t buf[SG_STORE_SIZE]);

/* Parse buf into s. Verifies magic, version, total_len, crc, then calls
 * sg_state_sanitize. On SG_STORE_E_CORRUPT *s is left as sg_state_defaults(). */
int sg_store_decode(const uint8_t buf[SG_STORE_SIZE], SgState *s);

/* Load from path. Returns SG_STORE_OK, or an SG_STORE_E_* code with *s set to
 * defaults. Never throws, never allocates. */
int sg_store_load(const char *path, SgState *s);

/* Atomic save (see header comment). Returns SG_STORE_OK or SG_STORE_E_*. */
int sg_store_save(const char *path, const SgState *s);

/* CRC32 as specified above; exposed for tests. */
uint32_t sg_store_crc32(const uint8_t *data, size_t len);

#endif /* SG_STORE_H */
