/* Baseline JPEG decoder for BoxOS. Header-only, included once from
 * winimg.c. Scope:
 *   - SOF0 (baseline DCT, 8-bit)
 *   - 1 or 3 components, 4:4:4 / 4:2:2 / 4:2:0 chroma sampling
 *   - DQT (8-bit), DHT (16-bit), DRI / RSTn restart support
 *   - 1-bit-at-a-time Huffman decode (slow but obviously correct)
 *   - x87 floating-point 8-point IDCT (clear math)
 *
 * Out of scope: progressive (SOF2), arithmetic, hierarchical, CMYK,
 * 12-bit precision, JFIF/EXIF metadata beyond skipping APPn.
 *
 * jpeg_decode(buf, size, &rgb, &w, &h) returns 0 on success and
 * fills the out parameters. Caller xfree()s rgb.
 */

#ifndef BOXOS_JPEG_H
#define BOXOS_JPEG_H

#include "boxos_app.h"

/* ---- bit reader ----------------------------------------------- */

static const uint8_t* j_pos;
static const uint8_t* j_end;
static int j_bits;        /* shift register */
static int j_n_bits;
static int j_err;
static int j_marker;      /* 0 or the marker byte that ended the bit-stream */

static int j_read_byte(void) {
    if (j_pos >= j_end) { j_err = 1; return 0; }
    return *j_pos++;
}

static int j_read16(void) {
    int hi = j_read_byte();
    int lo = j_read_byte();
    return (hi << 8) | lo;
}

static int j_get_bit(void) {
    if (j_n_bits == 0) {
        int b = j_read_byte();
        if (b == 0xFF) {
            int m = j_read_byte();
            if (m != 0x00) {
                j_marker = m;
                j_err = 1;
                return 0;
            }
        }
        j_bits = b;
        j_n_bits = 8;
    }
    int v = (j_bits >> 7) & 1;
    j_bits = (j_bits << 1) & 0xFF;
    j_n_bits--;
    return v;
}

static int j_get_bits(int n) {
    int v = 0;
    for (int i = 0; i < n; i++) v = (v << 1) | j_get_bit();
    return v;
}

static void j_byte_align(void) { j_bits = 0; j_n_bits = 0; }

static int j_extend(int v, int n) {
    if (n == 0) return 0;
    if (v < (1 << (n - 1))) return v - ((1 << n) - 1);
    return v;
}

/* ---- Huffman --------------------------------------------------- */

struct j_huff {
    int n;
    int first_code[17];
    int first_idx[17];
    int last_idx[17];
    uint8_t vals[256];
};

/* slot = (Tc<<1) | Th: 0=DCY 1=DCC 2=ACY 3=ACC */
static struct j_huff j_huff[4];

static int j_load_dht(const uint8_t* p, int len) {
    while (len > 0) {
        if (len < 17) return -1;
        int Tc = (p[0] >> 4) & 1;
        int Th = p[0] & 1;
        int slot = (Tc << 1) | Th;
        struct j_huff* h = &j_huff[slot];
        int cnt[17] = {0};
        int total = 0;
        for (int L = 1; L <= 16; L++) { cnt[L] = p[L]; total += cnt[L]; }
        p += 17; len -= 17;
        if (total > 256 || len < total) return -1;
        h->n = total;
        for (int i = 0; i < total; i++) h->vals[i] = p[i];
        p += total; len -= total;
        int code = 0, idx = 0;
        for (int L = 1; L <= 16; L++) {
            h->first_code[L] = code;
            h->first_idx[L] = idx;
            for (int j = 0; j < cnt[L]; j++) { code++; idx++; }
            h->last_idx[L] = idx;
            code <<= 1;
        }
    }
    return 0;
}

static int j_decode_huff(struct j_huff* h) {
    int code = 0;
    for (int L = 1; L <= 16; L++) {
        code = (code << 1) | j_get_bit();
        if (j_err) return 0;
        int delta = code - h->first_code[L];
        int avail = h->last_idx[L] - h->first_idx[L];
        if (delta >= 0 && delta < avail) return h->vals[h->first_idx[L] + delta];
    }
    j_err = 1;
    return 0;
}

/* ---- Quant tables --------------------------------------------- */

static int j_qt[4][64];

static int j_load_dqt(const uint8_t* p, int len) {
    while (len > 0) {
        int pq = (p[0] >> 4) & 0x0F;
        int tq = p[0] & 0x0F;
        if (tq >= 4 || pq != 0) return -1;
        if (len < 65) return -1;
        for (int i = 0; i < 64; i++) j_qt[tq][i] = p[1 + i];
        p += 65; len -= 65;
    }
    return 0;
}

/* ---- Float IDCT ----------------------------------------------- */

/* Standard 8-point IDCT, applied row-then-column. cos(k*pi/16) */
static const float j_c1 = 0.98078528040323043f;
static const float j_c2 = 0.92387953251128674f;
static const float j_c3 = 0.83146961230254524f;
static const float j_c4 = 0.70710678118654757f;
static const float j_c5 = 0.55557023301960229f;
static const float j_c6 = 0.38268343236508984f;
static const float j_c7 = 0.19509032201612833f;

static void j_idct8(float* d) {
    /* Inverse DCT-II of 8 samples in d, in-place. Straight O(N^2)
     * implementation — clear, correct, ~64 muls per call. */
    float out[8];
    for (int n = 0; n < 8; n++) {
        float sum = d[0] * j_c4 * 0.5f;
        for (int k = 1; k < 8; k++) {
            float cos_k = 0.0f;
            /* Compute cos((2n+1)*k*pi/16) using lookup. */
            int idx = ((2*n + 1) * k) & 31;     /* mod 32 */
            /* cos((m * pi/16)) for m = 0..15 is known; for m>=16
             * cos(m*pi/16) = -cos((m-16)*pi/16). */
            int sign = 1;
            if (idx >= 16) { sign = -1; idx -= 16; }
            float lut[16] = {
                1.0f, j_c1, j_c2, j_c3, j_c4, j_c5, j_c6, j_c7,
                0.0f, -j_c7, -j_c6, -j_c5, -j_c4, -j_c3, -j_c2, -j_c1,
            };
            cos_k = sign * lut[idx];
            sum += d[k] * cos_k * 0.5f;
        }
        out[n] = sum;
    }
    for (int i = 0; i < 8; i++) d[i] = out[i];
}

static int j_clamp(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

static void j_idct(int* coef_in, uint8_t* out, int out_stride) {
    /* Float buffer. Multiply by quant table happens at caller. */
    float blk[64];
    for (int i = 0; i < 64; i++) blk[i] = (float)coef_in[i];
    /* Row pass */
    for (int r = 0; r < 8; r++) j_idct8(&blk[r * 8]);
    /* Column pass — gather, IDCT, scatter. */
    for (int c = 0; c < 8; c++) {
        float col[8];
        for (int r = 0; r < 8; r++) col[r] = blk[r * 8 + c];
        j_idct8(col);
        for (int r = 0; r < 8; r++) blk[r * 8 + c] = col[r];
    }
    /* Level-shift and clamp. */
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            int v = (int)(blk[r * 8 + c] + 128.5f);
            out[r * out_stride + c] = (uint8_t)j_clamp(v);
        }
    }
}

/* ---- frame --------------------------------------------------- */

#define J_MAX_C 3

struct j_comp {
    int id, hs, vs, qi;
    int dc_ti, ac_ti;
    int dc_pred;
    uint8_t* plane;
    int stride, w, h;
};
static struct j_comp j_comp[J_MAX_C];
static int j_nc, j_w, j_h, j_max_h, j_max_v;
static int j_dri;

static const uint8_t j_zigzag[64] = {
     0, 1, 8,16, 9, 2, 3,10,
    17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34,
    27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36,
    29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46,
    53,60,61,54,47,55,62,63,
};

static int j_load_sof(const uint8_t* p, int len) {
    if (len < 6) return -1;
    if (p[0] != 8) return -1;
    j_h = (p[1] << 8) | p[2];
    j_w = (p[3] << 8) | p[4];
    int n = p[5];
    if (n != 1 && n != 3) return -1;
    if (len < 6 + n * 3) return -1;
    j_nc = n;
    p += 6;
    j_max_h = j_max_v = 1;
    for (int i = 0; i < n; i++) {
        j_comp[i].id = p[0];
        j_comp[i].hs = (p[1] >> 4) & 0x0F;
        j_comp[i].vs = p[1] & 0x0F;
        j_comp[i].qi = p[2];
        if (j_comp[i].hs > j_max_h) j_max_h = j_comp[i].hs;
        if (j_comp[i].vs > j_max_v) j_max_v = j_comp[i].vs;
        p += 3;
    }
    return 0;
}

static int j_load_sos(const uint8_t* p, int len) {
    if (len < 1) return -1;
    int ns = p[0];
    if (ns != j_nc) return -1;
    if (len < 1 + ns * 2 + 3) return -1;
    p++;
    for (int i = 0; i < ns; i++) {
        int Cs = p[0];
        int td = (p[1] >> 4) & 0x0F;
        int ta = p[1] & 0x0F;
        for (int k = 0; k < j_nc; k++) {
            if (j_comp[k].id == Cs) {
                j_comp[k].dc_ti = (0 << 1) | td;        /* DC slot 0/1 */
                j_comp[k].ac_ti = (1 << 1) | ta;        /* AC slot 2/3 */
                break;
            }
        }
        p += 2;
    }
    /* Ss/Se/AhAl ignored for baseline. */
    return 0;
}

static int j_decode_block(struct j_comp* c, uint8_t* dst, int stride) {
    int coef[64];
    for (int i = 0; i < 64; i++) coef[i] = 0;
    int t = j_decode_huff(&j_huff[c->dc_ti]);
    int dd = t ? j_extend(j_get_bits(t), t) : 0;
    if (j_err) return -1;
    c->dc_pred += dd;
    coef[0] = c->dc_pred * j_qt[c->qi][0];
    int k = 1;
    while (k < 64) {
        int rs = j_decode_huff(&j_huff[c->ac_ti]);
        if (j_err) return -1;
        int rrrr = (rs >> 4) & 0x0F;
        int ssss = rs & 0x0F;
        if (ssss == 0) {
            if (rrrr == 0) break;
            if (rrrr == 15) k += 16;
            else return -1;
            continue;
        }
        k += rrrr;
        if (k >= 64) return -1;
        int v = j_extend(j_get_bits(ssss), ssss);
        coef[j_zigzag[k]] = v * j_qt[c->qi][k];
        k++;
    }
    j_idct(coef, dst, stride);
    return 0;
}

static int j_alloc_planes(void) {
    for (int i = 0; i < j_nc; i++) {
        struct j_comp* c = &j_comp[i];
        int w = (j_w * c->hs + j_max_h - 1) / j_max_h;
        int h = (j_h * c->vs + j_max_v - 1) / j_max_v;
        int aw = ((w + c->hs * 8 - 1) / (c->hs * 8)) * c->hs * 8;
        int ah = ((h + c->vs * 8 - 1) / (c->vs * 8)) * c->vs * 8;
        c->w = w; c->h = h; c->stride = aw;
        c->plane = (uint8_t*)xmalloc((uint64_t)aw * ah);
        if (!c->plane) return -1;
    }
    return 0;
}

static int j_scan(void) {
    int mw = j_max_h * 8, mh = j_max_v * 8;
    int nx = (j_w + mw - 1) / mw, ny = (j_h + mh - 1) / mh;
    for (int i = 0; i < j_nc; i++) j_comp[i].dc_pred = 0;
    int rst = j_dri;
    for (int my = 0; my < ny; my++) {
        for (int mx = 0; mx < nx; mx++) {
            for (int ci = 0; ci < j_nc; ci++) {
                struct j_comp* c = &j_comp[ci];
                for (int by = 0; by < c->vs; by++) {
                    for (int bx = 0; bx < c->hs; bx++) {
                        int px = mx * c->hs * 8 + bx * 8;
                        int py = my * c->vs * 8 + by * 8;
                        if (j_decode_block(c, c->plane + py * c->stride + px,
                                            c->stride) < 0) return -1;
                    }
                }
            }
            if (j_dri && --rst == 0) {
                rst = j_dri;
                /* No restart marker after the very last MCU — the
                 * stream goes straight to EOI. Skip the consume to
                 * avoid eating into FF D9. */
                int last_mcu = (my == ny - 1) && (mx == nx - 1);
                if (!last_mcu) {
                    j_byte_align();
                    /* At an MCU boundary the bit reader hasn't pulled
                     * the upcoming RSTn marker yet — j_err is still
                     * clean. Consume FF Dx (0xD0..0xD7) directly so
                     * the next MCU's first j_get_bit starts on fresh
                     * scan data. */
                    int b = j_read_byte();
                    if (b != 0xFF) return -1;
                    int m;
                    do { m = j_read_byte(); } while (m == 0xFF);
                    if (m < 0xD0 || m > 0xD7) return -1;
                    for (int i = 0; i < j_nc; i++) j_comp[i].dc_pred = 0;
                }
            }
        }
    }
    return 0;
}

/* Upsample chroma + YCbCr->RGB. */
static int j_to_rgb(uint8_t** out_rgb, int* out_w, int* out_h) {
    int W = j_w, H = j_h;
    *out_w = W; *out_h = H;
    *out_rgb = (uint8_t*)xmalloc((uint64_t)W * H * 3);
    if (!*out_rgb) return -1;
    if (j_nc == 1) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int Y = j_comp[0].plane[y * j_comp[0].stride + x];
                int o = (y * W + x) * 3;
                (*out_rgb)[o] = (*out_rgb)[o+1] = (*out_rgb)[o+2] = (uint8_t)Y;
            }
        return 0;
    }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int Y  = j_comp[0].plane[y * j_comp[0].stride + x];
            int cx1 = x * j_comp[1].hs / j_max_h;
            int cy1 = y * j_comp[1].vs / j_max_v;
            int cx2 = x * j_comp[2].hs / j_max_h;
            int cy2 = y * j_comp[2].vs / j_max_v;
            int Cb = j_comp[1].plane[cy1 * j_comp[1].stride + cx1] - 128;
            int Cr = j_comp[2].plane[cy2 * j_comp[2].stride + cx2] - 128;
            /* Standard JFIF YCbCr -> RGB. */
            int R = Y + ((91881 * Cr + 32768) >> 16);
            int G = Y - ((22554 * Cb + 46802 * Cr + 32768) >> 16);
            int B = Y + ((116130 * Cb + 32768) >> 16);
            int o = (y * W + x) * 3;
            (*out_rgb)[o]   = (uint8_t)j_clamp(R);
            (*out_rgb)[o+1] = (uint8_t)j_clamp(G);
            (*out_rgb)[o+2] = (uint8_t)j_clamp(B);
        }
    }
    return 0;
}

/* Public entry point. */
static int jpeg_decode(const uint8_t* buf, int size,
                       uint8_t** out_rgb, int* out_w, int* out_h) {
    j_pos = buf; j_end = buf + size;
    j_err = 0; j_marker = 0; j_bits = 0; j_n_bits = 0;
    j_dri = 0;
    for (int i = 0; i < J_MAX_C; i++) j_comp[i].plane = 0;

    /* SOI */
    if (j_read_byte() != 0xFF || j_read_byte() != 0xD8) return -1;

    int seen_sof = 0;
    int rc = -1;

    while (!j_err) {
        int b = j_read_byte();
        if (b != 0xFF) { rc = -1; break; }
        int m;
        do { m = j_read_byte(); } while (m == 0xFF);
        if (j_err) break;
        if (m == 0xD9) { rc = -1; break; }     /* EOI before SOS — fail */
        if (m >= 0xD0 && m <= 0xD7) continue;  /* stray RSTn */
        int len = j_read16();
        if (j_err || len < 2) break;
        int payload_len = len - 2;
        if (j_pos + payload_len > j_end) break;
        const uint8_t* seg = j_pos;
        int handled = 1;
        switch (m) {
            case 0xDB: if (j_load_dqt(seg, payload_len) < 0) goto fail; break;
            case 0xC0: if (j_load_sof(seg, payload_len) < 0) goto fail;
                       seen_sof = 1; break;
            case 0xC4: if (j_load_dht(seg, payload_len) < 0) goto fail; break;
            case 0xDD:
                if (payload_len < 2) goto fail;
                j_dri = (seg[0] << 8) | seg[1];
                break;
            case 0xDA:
                if (!seen_sof) goto fail;
                if (j_load_sos(seg, payload_len) < 0) goto fail;
                if (j_alloc_planes() < 0) goto fail;
                j_pos += payload_len;
                if (j_scan() < 0) goto fail;
                rc = j_to_rgb(out_rgb, out_w, out_h);
                goto done;
            default:
                /* APPn / COM / DNL / unknown -- skip */
                handled = 1;
                break;
        }
        (void)handled;
        j_pos += payload_len;
    }
fail:
    rc = -1;
done:
    for (int i = 0; i < J_MAX_C; i++) {
        if (j_comp[i].plane) { xfree(j_comp[i].plane); j_comp[i].plane = 0; }
    }
    return rc;
}

#endif
