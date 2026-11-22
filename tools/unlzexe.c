/* SPDX-License-Identifier: MIT */
/* unlzexe.c - LZEXE 0.91 unpacker (clean-room, from the public format description).
 * Build: gcc -O2 -o unlzexe unlzexe.c
 * Use:   unlzexe PACKED.EXE UNPACKED.EXE
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint8_t *in; static size_t insz, ipos;
static uint16_t bitbuf; static int bitcount;

static int getb(void){ return ipos < insz ? in[ipos++] : -1; }
static uint16_t get16(void){ int a = getb(), b = getb(); return (uint16_t)(a | (b << 8)); }
static void initbits(void){ bitbuf = get16(); bitcount = 16; }
static int getbit(void){
    int b = bitbuf & 1;
    if (--bitcount == 0) { bitbuf = get16(); bitcount = 16; } else bitbuf >>= 1;
    return b;
}
static uint16_t rw(const uint8_t *p){ return p[0] | (p[1] << 8); }

int main(int argc, char **argv){
    if (argc < 3){ fprintf(stderr, "usage: %s in.exe out.exe\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb"); if(!f){ perror("open"); return 1; }
    fseek(f, 0, SEEK_END); insz = ftell(f); fseek(f, 0, SEEK_SET);
    in = malloc(insz); fread(in, 1, insz, f); fclose(f);

    uint16_t h[0x0e]; for (int i = 0; i < 0x0e; i++) h[i] = rw(in + i*2);
    if (h[0] != 0x5a4d || h[0x0c] != 0x1c || h[0x0d] != 0 || memcmp(in + 0x1c, "LZ91", 4)) {
        fprintf(stderr, "not an LZEXE 0.91 file\n"); return 2; }

    long cs_off = ((long)h[0x0b] + h[4]) << 4;
    uint16_t inf[8]; for (int i = 0; i < 8; i++) inf[i] = rw(in + cs_off + i*2);
    /* inf: ip, cs, sp, ss, packed size(paras), growth(paras), decomp size(bytes), -- */
    fprintf(stderr, "entry %04X:%04X stack %04X:%04X packed=%u paras grow=%u decompsz=%u\n",
            inf[1], inf[0], inf[3], inf[2], inf[4], inf[5], inf[6]);

    size_t outcap = 0x100000; uint8_t *out = calloc(1, outcap); size_t p = 0;
    ipos = ((long)h[0x0b] - inf[4] + h[4]) << 4;
    initbits();
    for (;;) {
        if (ipos > insz) { fprintf(stderr, "ran off input\n"); return 3; }
        if (getbit()) { out[p++] = (uint8_t)getb(); continue; }
        int len; int span;
        if (!getbit()) {
            len = getbit() << 1; len |= getbit(); len += 2;
            span = getb() | 0xff00;
        } else {
            span = getb();
            len = getb();
            span |= ((len & ~0x07) << 5) | 0xe000;
            len = (len & 0x07) + 2;
            if (len == 2) {
                len = getb();
                if (len == 0) break;       /* end marker */
                if (len == 1) continue;    /* segment change: irrelevant with linear buffer */
                len++;
            }
        }
        span = (int16_t)(uint16_t)span;
        for (; len > 0; len--, p++) out[p] = out[p + span];
        if (p + 0x400 > outcap) { fprintf(stderr, "output overflow\n"); return 4; }
    }
    fprintf(stderr, "decompressed %zu bytes\n", p);

    /* relocations (0.91): table at cs_off + 0x158 */
    uint16_t *rel = malloc(65536 * 4); int nrel = 0;
    size_t rp = cs_off + 0x158; uint32_t rseg = 0, roff = 0;
    for (;;) {
        if (rp >= insz) { fprintf(stderr, "reloc table ran off file\n"); return 5; }
        uint32_t span = in[rp++];
        if (span == 0) {
            span = in[rp] | (in[rp+1] << 8); rp += 2;
            if (span == 0) { rseg += 0x0fff; continue; }
            if (span == 1) break;
        }
        roff += span; rseg += (roff & ~0x0fu) >> 4; roff &= 0x0f;
        rel[nrel*2] = (uint16_t)roff; rel[nrel*2+1] = (uint16_t)rseg; nrel++;
    }
    fprintf(stderr, "%d relocations\n", nrel);

    uint16_t o[0x0e]; memcpy(o, h, sizeof o);
    o[0x0a] = inf[0]; o[0x0b] = inf[1]; o[0x08] = inf[2]; o[0x07] = inf[3];
    o[0x0c] = 0x1c; o[3] = (uint16_t)nrel;
    int hdrbytes = (0x1c + nrel*4 + 15) & ~15; o[4] = hdrbytes >> 4;
    size_t total = hdrbytes + p;
    o[1] = total % 512; o[2] = (total + 511) / 512; if (o[1] == 0) { /* full last page */ }
    o[5] = h[5] - inf[5] - ((inf[6] + 15) >> 4) - 9;
    if (h[6] != 0xffff) o[6] = h[6] - (h[5] - o[5]);
    o[9] = 0;

    FILE *w = fopen(argv[2], "wb");
    fwrite(o, 2, 0x0e, w);
    fwrite(rel, 4, nrel, w);
    for (int i = 0x1c + nrel*4; i < hdrbytes; i++) fputc(0, w);
    fwrite(out, 1, p, w); fclose(w);
    fprintf(stderr, "wrote %s (%zu bytes)\n", argv[2], total);
    return 0;
}
