/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of firmware/src/storage.c against a simulated NOR flash:
 * erase -> 0xFF, program can only clear bits, page writes must not wrap. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint8_t nor[0x100000];
static int fail_after = -1;            /* torn-write injection: stop after N programs */

static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    if ((off & 0xFFu) + n > 256u) {
        printf("page wrap at %#x\n", off);
        exit(1);
    }
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
#include "../firmware/src/storage.c"

static int check(const char *what, int ok)
{
    printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

int main(void)
{
    char a[600], b[600], got[600];
    int bad = 0, n;
    memset(nor, 0xFF, sizeof nor);
    memset(a, 'A', sizeof a);
    memset(b, 'B', sizeof b);
    bad += check("empty flash loads nothing", st_load(OBJ_PROJECT0, got, sizeof got) < 0);
    bad += check("save A", st_save(OBJ_PROJECT0, a, sizeof a) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save B", st_save(OBJ_PROJECT0, b, sizeof b) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns B (newer seq)", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 1;                                  /* payload page 1 written, the rest torn */
    st_save(OBJ_PROJECT0, a, sizeof a);
    fail_after = -1;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("torn save keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 3;                                  /* all payload pages, header torn */
    st_save(OBJ_PROJECT0, a, sizeof a);
    fail_after = -1;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("header not written keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    bad += check("save A again", st_save(OBJ_PROJECT0, a, sizeof a) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    nor[st_sector(OBJ_PROJECT0, 0) + ST_PAYLOAD_OFF + 10] ^= 0x01;   /* bit rot in one copy */
    nor[st_sector(OBJ_PROJECT0, 1) + ST_PAYLOAD_OFF + 10] ^= 0x01;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("both copies corrupt -> nothing", n < 0);
    bad += check("other objects untouched", st_load(OBJ_PROJECT0 + 1, got, sizeof got) < 0);
    bad += check("settings save/load",
                 st_save(OBJ_SETTINGS, "hello", 5) == 0 && st_load(OBJ_SETTINGS, got, 5) == 5 && !memcmp(got, "hello", 5));
    bad += check("CRC-32 is zlib's (check value 0xCBF43926)", st_crc32("123456789", 9) == 0xCBF43926u);
    memset(nor, 0xFF, sizeof nor);
    st_save(OBJ_PROJECT0 + 2, a, sizeof a);
    st_save(OBJ_PROJECT0 + 2, b, sizeof b);              /* B is newer, in the other copy */
    {
        st_hdr_t h;
        int cur = st_current(OBJ_PROJECT0 + 2, &h);
        nor[st_sector(OBJ_PROJECT0 + 2, (uint32_t)cur) + ST_PAYLOAD_OFF + 300] ^= 0x10;   /* rot in the newer copy */
    }
    n = st_load(OBJ_PROJECT0 + 2, got, sizeof got);
    bad += check("newer copy rotten -> the older one loads", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save over the rotten copy", st_save(OBJ_PROJECT0 + 2, b, sizeof b) == 0);
    n = st_load(OBJ_PROJECT0 + 2, got, sizeof got);
    bad += check("... loads the new data", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    nor[st_sector(OBJ_PROJECT0 + 2, 0) + 8] ^= 0x01;    /* both headers broken */
    nor[st_sector(OBJ_PROJECT0 + 2, 1) + 8] ^= 0x01;
    bad += check("both headers broken -> nothing", st_load(OBJ_PROJECT0 + 2, got, sizeof got) < 0);
    {   /* NoteSorcery: every copy of every object (a project: two sectors) in the store's regions, off the
         * sample slots (USR1..3 0xA0000..0xCFFFF, USR4 0xE7000..0xF6FFF) and the update staging (0xE0000..0xE4FFF),
         * and no two copies sharing a sector */
        uint32_t o, c, o2, c2, inside = 1, apart = 1;
        for (o = 0; o < OBJ_COUNT; o++)
            for (c = 0; c < 2u; c++) {
                uint32_t a = st_sector(o, c), n = st_big(o) ? ST_BIG_SECTORS * 4096u : 4096u;
                int data = a >= 0x97000u && a + n <= 0xA0000u, ups = a >= 0xDC000u && a + n <= 0xE0000u;
                int secs = (a >= 0xD0000u && a + n <= 0xDC000u) || (a >= 0xF7000u && a + n <= 0xFB000u);
                int glob = a >= 0xFC000u && a + n <= 0xFF000u, fm6 = a >= 0xE5000u && a + n <= 0xE7000u;
                inside &= (data || ups || secs || glob || fm6) && !(a & 0xFFFu);
                for (o2 = 0; o2 < OBJ_COUNT; o2++)
                    for (c2 = 0; c2 < 2u; c2++) {
                        uint32_t b = st_sector(o2, c2), m = st_big(o2) ? ST_BIG_SECTORS * 4096u : 4096u;
                        if ((o2 != o || c2 != c) && b < a + n && a < b + m)
                            apart = 0;
                    }
            }
        bad += check("data stays in the store's regions (autosave too)", inside);
        bad += check("every object copy has its own sectors", apart);
    }
    {   /* NoteSorcery: a project takes two sectors; saved, torn, loaded */
        static uint8_t big[7604], big2[7604], gotb[7604];
        uint32_t i;
        memset(nor, 0xFF, sizeof nor);
        for (i = 0; i < sizeof big; i++) {
            big[i] = (uint8_t)(i * 7u + 3u);
            big2[i] = (uint8_t)(i * 13u + 1u);
        }
        bad += check("big: a project (7604 B) saves", st_save(OBJ_PROJECT0 + 3, big, sizeof big) == 0);
        n = st_load(OBJ_PROJECT0 + 3, gotb, sizeof gotb);
        bad += check("big: ... and loads whole", n == (int)sizeof big && !memcmp(gotb, big, sizeof big));
        fail_after = 20;                              /* the second save torn in its second sector */
        st_save(OBJ_PROJECT0 + 3, big2, sizeof big2);
        fail_after = -1;
        n = st_load(OBJ_PROJECT0 + 3, gotb, sizeof gotb);
        bad += check("big: a torn save keeps the last one", n == (int)sizeof big && !memcmp(gotb, big, sizeof big));
        bad += check("big: saves again", st_save(OBJ_PROJECT0 + 3, big2, sizeof big2) == 0);
        n = st_load(OBJ_PROJECT0 + 3, gotb, sizeof gotb);
        bad += check("big: ... the newer copy loads", n == (int)sizeof big2 && !memcmp(gotb, big2, sizeof big2));
        bad += check("big: the payload in place (XIP reads it there)",
                     st_payload_off(OBJ_PROJECT0 + 3) && !memcmp(nor + st_payload_off(OBJ_PROJECT0 + 3), big2, sizeof big2));
        nor[st_payload_off(OBJ_PROJECT0 + 3) + 5000u] ^= 0x40u;   /* rot in its second sector */
        n = st_load(OBJ_PROJECT0 + 3, gotb, sizeof gotb);
        bad += check("big: rot in the newer copy -> the older one", n == (int)sizeof big && !memcmp(gotb, big, sizeof big));
        bad += check("big: too big for two sectors: refused", st_save(OBJ_PROJECT0, nor, ST_BIG_PAYLOAD_MAX + 1u) == -1);
        bad += check("big: a one-sector object stays one sector", st_save(OBJ_SETTINGS, big, ST_PAYLOAD_MAX + 1u) == -1);
    }
    {   /* SLOOP 2.5: the settings in two parts (persist_t, then the SYN kits): one record, A/B as any other */
        static char p1[88], p2[1464], all[88 + 1464], got2[88 + 1464];
        memset(p1, 'P', sizeof p1);
        memset(p2, 'K', sizeof p2);
        memcpy(all, p1, sizeof p1);
        memcpy(all + sizeof p1, p2, sizeof p2);
        bad += check("2.5: save in two parts", st_save2(OBJ_SETTINGS, p1, sizeof p1, p2, sizeof p2) == 0);
        n = st_load(OBJ_SETTINGS, got2, sizeof got2);
        bad += check("2.5: ... loads as one object", n == (int)sizeof all && !memcmp(got2, all, sizeof all));
        n = st_load(OBJ_SETTINGS, got2, sizeof p1);
        bad += check("2.5: ... a 2.4 load takes its part only", n == (int)sizeof p1 && !memcmp(got2, p1, sizeof p1));
        memset(p2, 'L', sizeof p2);
        fail_after = 4;                              /* a torn second save: the first copy stays */
        st_save2(OBJ_SETTINGS, p1, sizeof p1, p2, sizeof p2);
        fail_after = -1;
        n = st_load(OBJ_SETTINGS, got2, sizeof got2);
        bad += check("2.5: ... a torn save keeps the last one", n == (int)sizeof all && !memcmp(got2, all, sizeof all));
        bad += check("2.5: ... too big for a sector: refused", st_save2(OBJ_SETTINGS, p2, 3000, p2, 1000) == -1);
    }
    printf("%s\n", bad ? "STORAGE TEST FAILED" : "storage test passed");
    return bad != 0;
}
