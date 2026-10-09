/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Persistent storage on the SPI NOR.
 *
 * Every object has an A/B sector pair. A save goes to the copy that is not
 * the current one: erase the sector, program the payload (from offset 256),
 * then the 32-byte header at offset 0 last. The header is the commit record;
 * on load the valid copy with the highest seq wins, so a torn write leaves
 * the previous copy in charge.
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 */
#define ST_MAGIC 0x554C4546u                   /* "FELU" */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAYLOAD_MAX (ST_SECTOR - ST_PAYLOAD_OFF)   /* a one-sector object (st_buf holds it) */
#define ST_BIG_SECTORS 2u                      /* NoteSorcery: a project (8 tracks) takes two sectors a copy */
#define ST_BIG_PAYLOAD_MAX (ST_BIG_SECTORS * ST_SECTOR - ST_PAYLOAD_OFF)

/* flash map (NoteSorcery; FL_DATA 0x97000..0xDFFFF, FL_USR4 0xE7000..0xFAFFF, FL_GLOB 0xFC000..):
 *   0x97000 / 0x99000   the working project (autosave, project.c), copies A / B, two sectors each
 *   0x9B000..0x9FFFF    free (5 sectors)
 *   0xA0000..0xCFFFF    user sample slots USR1..USR3, 64 KiB each (eng_sample.c)
 *   0xD0000..0xDBFFF    sections A..C (the projects), A / B copies of two sectors each
 *   0xDC000..0xDFFFF    user preset banks (upreset.c)
 *   0xE0000..0xE4FFF    update staging (ota.c)
 *   0xE5000 / 0xE6000   the FM6 patch bank (fm6_bank.c)
 *   0xE7000..0xF6FFF    user sample slot USR4, 64 KiB
 *   0xF7000 / 0xF9000   section D, A / B copies of two sectors each
 *   0xFC000 / 0xFD000   settings */
enum { OBJ_SETTINGS, OBJ_PROJECT0, OBJ_UPRESET0 = OBJ_PROJECT0 + 4, OBJ_AUTOSAVE = OBJ_UPRESET0 + 2, OBJ_FM6BANK, OBJ_COUNT };

typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;

static int st_read(uint32_t off, void *dst, uint32_t n);
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

static uint32_t st_crc32_up(uint32_t c, const void *p, uint32_t n)   /* zlib CRC-32 (~), 4 bits per step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return c;
}
static uint32_t st_crc32(const void *p, uint32_t n) { return ~st_crc32_up(0xFFFFFFFFu, p, n); }

static int st_big(uint32_t obj) { return (obj >= OBJ_PROJECT0 && obj < OBJ_PROJECT0 + 4u) || obj == OBJ_AUTOSAVE; }
static uint32_t st_max(uint32_t obj) { return st_big(obj) ? ST_BIG_PAYLOAD_MAX : ST_PAYLOAD_MAX; }

static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */
{
    if (obj == OBJ_SETTINGS)
        return 0xFC000u + copy * ST_SECTOR;
    if (obj == OBJ_AUTOSAVE)
        return 0x97000u + copy * ST_BIG_SECTORS * ST_SECTOR;
    if (obj == OBJ_FM6BANK)
        return 0xE5000u + copy * ST_SECTOR;
    if (obj >= OBJ_UPRESET0 && obj < OBJ_AUTOSAVE)
        return 0xDC000u + (obj - OBJ_UPRESET0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    if (obj == OBJ_PROJECT0 + 3u)
        return 0xF7000u + copy * ST_BIG_SECTORS * ST_SECTOR;
    return 0xD0000u + (obj - OBJ_PROJECT0) * 2u * ST_BIG_SECTORS * ST_SECTOR + copy * ST_BIG_SECTORS * ST_SECTOR;
}

static uint8_t st_buf[ST_PAYLOAD_MAX] __attribute__((aligned(4)));

static int st_head(uint32_t obj, uint32_t copy, st_hdr_t *h)   /* commit record valid: 0 */
{
    if (obj >= OBJ_COUNT || copy > 1u)
        return -1;
    if (st_read(st_sector(obj, copy), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != obj || h->slot != copy || h->len > st_max(obj) ||   /* (slot: the copy
                                                     * it was written to; after Felucca 1.0) */
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}

/* the payload's CRC: into dst (h->len bytes) when dst, else streamed through st_buf; CRC ok: 0 */
static int st_body_to(uint32_t obj, uint32_t copy, const st_hdr_t *h, void *dst)
{
    uint32_t base = st_sector(obj, copy) + ST_PAYLOAD_OFF, off, c = 0xFFFFFFFFu;
    if (dst) {
        if (st_read(base, dst, h->len))
            return -1;
        return st_crc32(dst, h->len) == h->crc ? 0 : -1;
    }
    for (off = 0; off < h->len; off += ST_PAYLOAD_MAX) {
        uint32_t n = h->len - off > ST_PAYLOAD_MAX ? ST_PAYLOAD_MAX : h->len - off;
        if (st_read(base + off, st_buf, n))
            return -1;
        c = st_crc32_up(c, st_buf, n);
    }
    return ~c == h->crc ? 0 : -1;
}
static int st_body(uint32_t obj, uint32_t copy, const st_hdr_t *h)   /* payload -> st_buf, CRC ok: 0 */
{
    return st_body_to(obj, copy, h, 0);
}

/* the current copy: the valid one with the highest seq (A on a tie), -1 when neither is valid. Headers
 * first, so only the winner's payload is read: into dst when given (up to its h.len), else through st_buf
 * (a one-sector object is left there; a big one only checked); *h gets its header. */
static int st_current_to(uint32_t obj, st_hdr_t *h, void *dst)
{
    st_hdr_t a, b;
    int va = st_head(obj, 0, &a) == 0, vb = st_head(obj, 1, &b) == 0;
    if (vb && (!va || b.seq > a.seq)) {
        if (st_body_to(obj, 1, &b, dst) == 0) {
            *h = b;
            return 1;
        }
        vb = 0;
    }
    if (va && st_body_to(obj, 0, &a, dst) == 0) {
        *h = a;
        return 0;
    }
    if (vb && st_body_to(obj, 1, &b, dst) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}
static int st_current(uint32_t obj, st_hdr_t *h) { return st_current_to(obj, h, 0); }

/* load object into dst (up to max bytes); returns the length, or -1. A big object goes straight into dst
 * (which must hold all of it), a one-sector one through st_buf */
static int st_load(uint32_t obj, void *dst, uint32_t max)
{
    uint32_t i;
    st_hdr_t h;
    if (st_big(obj)) {
        st_hdr_t a, b;
        int va = st_head(obj, 0, &a) == 0 && a.len <= max, vb = st_head(obj, 1, &b) == 0 && b.len <= max;
        if (vb && (!va || b.seq > a.seq) && st_body_to(obj, 1, &b, dst) == 0)
            return (int)b.len;
        if (va && st_body_to(obj, 0, &a, dst) == 0)
            return (int)a.len;
        if (vb && st_body_to(obj, 1, &b, dst) == 0)
            return (int)b.len;
        return -1;
    }
    if (st_current(obj, &h) < 0)
        return -1;
    if (h.len > max)
        h.len = max;
    for (i = 0; i < h.len; i++)
        ((uint8_t *)dst)[i] = st_buf[i];
    return (int)h.len;
}

/* an object in two parts, src then src2 (SLOOP 2.5: the settings and the SYN kits after them). A big object
 * (a project) is one part, programmed straight from src (RAM: the driver wants RAM sources) */
static int st_save2(uint32_t obj, const void *src, uint32_t len, const void *src2, uint32_t len2)
{
    uint32_t seq, base, off, k;
    int cur, rc, big = obj < OBJ_COUNT && st_big(obj);
    const uint8_t *from = st_buf;
    st_hdr_t h;
    if (obj >= OBJ_COUNT || len + len2 > st_max(obj) || (big && len2))
        return -1;
    {   /* the current copy (headers, CRCs), without loading a big payload */
        st_hdr_t a, b;
        int va = st_head(obj, 0, &a) == 0 && st_body(obj, 0, &a) == 0;
        int vb = st_head(obj, 1, &b) == 0 && st_body(obj, 1, &b) == 0;
        cur = vb && (!va || b.seq > a.seq) ? 1 : va ? 0 : -1;
        h = cur == 1 ? b : a;
    }
    seq = cur < 0 ? 0u : h.seq;
    base = st_sector(obj, cur == 0 ? 1u : 0u);       /* write the other copy */
    if (big) {
        from = (const uint8_t *)src;
    } else {
        for (off = 0; off < len; off++)
            st_buf[off] = ((const uint8_t *)src)[off];    /* the driver wants RAM sources */
        for (off = 0; off < len2; off++)
            st_buf[len + off] = ((const uint8_t *)src2)[off];
        len += len2;
    }
    for (k = 0; k < (big ? ST_BIG_SECTORS : 1u); k++)
        if ((rc = st_erase(base + k * ST_SECTOR)) != 0)
            return rc;
    for (off = 0; off < len; off += 256u) {
        uint32_t n = len - off > 256u ? 256u : len - off;
        if ((rc = st_prog(base + ST_PAYLOAD_OFF + off, from + off, n)) != 0)
            return rc;
    }
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)(cur == 0 ? 1 : 0);
    h.seq = seq + 1u;
    h.len = len;
    h.crc = st_crc32(from, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(base, &h, sizeof h)) != 0)       /* the commit record, last */
        return rc;
    {   /* read back: a write-protected or failing part must not report SAVED */
        st_hdr_t chk;
        uint32_t c = cur == 0 ? 1u : 0u;
        if (st_head(obj, c, &chk) || memcmp(&chk, &h, sizeof h) || st_body(obj, c, &chk))   /* the whole record */
            return -7;
    }
    return 0;
}
static int st_save(uint32_t obj, const void *src, uint32_t len) { return st_save2(obj, src, len, 0, 0); }
/* the payload of an object's current copy, read through XIP by the caller (sections: project.c sec_get); 0 = none */
static uint32_t st_payload_off(uint32_t obj)
{
    st_hdr_t h;
    int c = st_current(obj, &h);
    return c < 0 ? 0u : st_sector(obj, (uint32_t)c) + ST_PAYLOAD_OFF;
}
