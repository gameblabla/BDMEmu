#include "bdm_image_import.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BDM_IMPORT_W 160
#define BDM_IMPORT_H 120

typedef struct bdm_rgb_image {
    int w;
    int h;
    uint8_t *rgb;
} bdm_rgb_image_t;

static void set_status(char *status, size_t status_cap, const char *msg) {
    if (!status || !status_cap) return;
    if (!msg) msg = "";
    snprintf(status, status_cap, "%s", msg);
}

static void set_statusf(char *status, size_t status_cap, const char *fmt, int a, int b, int c, int d) {
    if (!status || !status_cap) return;
    snprintf(status, status_cap, fmt, a, b, c, d);
}

void bdm_fe_image_import_options_init(bdm_fe_image_import_options_t *opt) {
    if (!opt) return;
    memset(opt, 0, sizeof(*opt));
    opt->dither_strength = 0.85f;
}

static int pixel_dark_at(const uint32_t *fb, size_t w, int x, int y) {
    if (!fb || x < 0 || y < 0 || x >= BDM_IMPORT_W || y >= BDM_IMPORT_H) return 0;
    return bdm_fe_framebuffer_pixel_on(fb[(size_t)y * w + (size_t)x]);
}

int bdm_fe_detect_drawing_area(const bdm_video_t *video, bdm_fe_rect_t *out_rect, char *status, size_t status_cap) {
    size_t w = 0, h = 0;
    const uint32_t *fb = bdm_video_framebuffer(video, &w, &h);
    if (!out_rect) return -1;
    memset(out_rect, 0, sizeof(*out_rect));
    if (!fb || w < BDM_IMPORT_W || h < BDM_IMPORT_H) {
        set_status(status, status_cap, "no LCD framebuffer");
        return -1;
    }

    uint8_t visited[BDM_IMPORT_W * BDM_IMPORT_H];
    uint16_t qx[BDM_IMPORT_W * BDM_IMPORT_H];
    uint16_t qy[BDM_IMPORT_W * BDM_IMPORT_H];
    memset(visited, 0, sizeof(visited));

    int best_x = 1, best_y = 1, best_w = BDM_IMPORT_W - 2, best_h = BDM_IMPORT_H - 2;
    int best_score = -1;
    int best_conf = 0;
    int best_edge = 4;
    int fallback_x = 1, fallback_y = 1, fallback_w = BDM_IMPORT_W - 2, fallback_h = BDM_IMPORT_H - 2;
    int fallback_area = 0;

    for (int sy = 0; sy < BDM_IMPORT_H; ++sy) {
        for (int sx = 0; sx < BDM_IMPORT_W; ++sx) {
            int idx = sy * BDM_IMPORT_W + sx;
            if (visited[idx] || pixel_dark_at(fb, w, sx, sy)) continue;
            int head = 0, tail = 0;
            int minx = sx, maxx = sx, miny = sy, maxy = sy, count = 0;
            qx[tail] = (uint16_t)sx; qy[tail] = (uint16_t)sy; ++tail; visited[idx] = 1;
            while (head < tail) {
                int x = (int)qx[head];
                int y = (int)qy[head];
                ++head;
                ++count;
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
                const int dx[4] = { 1, -1, 0, 0 };
                const int dy[4] = { 0, 0, 1, -1 };
                for (int d = 0; d < 4; ++d) {
                    int nx = x + dx[d], ny = y + dy[d];
                    int ni;
                    if (nx < 0 || ny < 0 || nx >= BDM_IMPORT_W || ny >= BDM_IMPORT_H) continue;
                    ni = ny * BDM_IMPORT_W + nx;
                    if (visited[ni] || pixel_dark_at(fb, w, nx, ny)) continue;
                    visited[ni] = 1;
                    qx[tail] = (uint16_t)nx;
                    qy[tail] = (uint16_t)ny;
                    ++tail;
                }
            }

            int bw = maxx - minx + 1;
            int bh = maxy - miny + 1;
            int bbox_area = bw * bh;
            int edge = 0;
            if (minx <= 0) ++edge;
            if (miny <= 0) ++edge;
            if (maxx >= BDM_IMPORT_W - 1) ++edge;
            if (maxy >= BDM_IMPORT_H - 1) ++edge;
            if (bbox_area > fallback_area && bw >= 24 && bh >= 16) {
                fallback_area = bbox_area;
                fallback_x = minx; fallback_y = miny; fallback_w = bw; fallback_h = bh;
            }
            if (bw < 24 || bh < 16) continue;
            if (count * 10 < bbox_area * 5) continue; /* fragmented, not a light canvas */

            /* Prefer light components that are enclosed by UI/borders instead of whole-screen background. */
            int border_dark = 0;
            int border_total = 0;
            for (int x = minx - 1; x <= maxx + 1; ++x) {
                if (x >= 0 && x < BDM_IMPORT_W) {
                    if (pixel_dark_at(fb, w, x, miny - 1)) ++border_dark;
                    if (pixel_dark_at(fb, w, x, maxy + 1)) ++border_dark;
                    border_total += 2;
                }
            }
            for (int y = miny - 1; y <= maxy + 1; ++y) {
                if (y >= 0 && y < BDM_IMPORT_H) {
                    if (pixel_dark_at(fb, w, minx - 1, y)) ++border_dark;
                    if (pixel_dark_at(fb, w, maxx + 1, y)) ++border_dark;
                    border_total += 2;
                }
            }
            int border_pct = border_total ? (border_dark * 100) / border_total : 0;
            int score = bbox_area;
            score += border_pct * 20;
            score -= edge * bbox_area / 2;
            if (edge >= 3) score -= bbox_area;
            if (score > best_score) {
                best_score = score;
                best_x = minx; best_y = miny; best_w = bw; best_h = bh;
                best_edge = edge;
                best_conf = 45 + border_pct / 2 - edge * 10;
                if (best_conf > 95) best_conf = 95;
                if (best_conf < 20) best_conf = 20;
            }
        }
    }

    if (best_score < 0) {
        best_x = fallback_x;
        best_y = fallback_y;
        best_w = fallback_w;
        best_h = fallback_h;
        best_conf = fallback_area ? 15 : 5;
        best_edge = 4;
    }

    /* Avoid drawing on the black frame itself when a component touches a border. */
    if (best_x <= 0) { best_x = 1; --best_w; }
    if (best_y <= 0) { best_y = 1; --best_h; }
    if (best_x + best_w >= BDM_IMPORT_W) best_w = BDM_IMPORT_W - best_x - 1;
    if (best_y + best_h >= BDM_IMPORT_H) best_h = BDM_IMPORT_H - best_y - 1;
    if (best_w < 8 || best_h < 8) { best_x = 1; best_y = 1; best_w = BDM_IMPORT_W - 2; best_h = BDM_IMPORT_H - 2; best_conf = 5; }

    out_rect->x = best_x;
    out_rect->y = best_y;
    out_rect->w = best_w;
    out_rect->h = best_h;
    out_rect->confidence = best_conf;
    (void)best_edge;
    set_statusf(status, status_cap, "detected drawing area %d,%d %dx%d", best_x, best_y, best_w, best_h);
    return 0;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static int32_t rds32(const uint8_t *p) { return (int32_t)rd32(p); }

static int load_bmp(const uint8_t *data, size_t size, bdm_rgb_image_t *out, char *status, size_t status_cap) {
    if (size < 54 || data[0] != 'B' || data[1] != 'M') return 0;
    uint32_t off = rd32(data + 10);
    uint32_t dib = rd32(data + 14);
    if (dib < 40 || size < 14u + dib || off >= size) { set_status(status, status_cap, "unsupported BMP header"); return -1; }
    int32_t w = rds32(data + 18);
    int32_t h_signed = rds32(data + 22);
    uint16_t planes = rd16(data + 26);
    uint16_t bpp = rd16(data + 28);
    uint32_t comp = rd32(data + 30);
    if (planes != 1 || comp != 0 || w <= 0 || h_signed == 0 || w > 8192 || h_signed > 8192 || h_signed < -8192) {
        set_status(status, status_cap, "only uncompressed RGB BMP is supported");
        return -1;
    }
    int h = h_signed < 0 ? -h_signed : h_signed;
    int top_down = h_signed < 0;
    if (bpp != 24 && bpp != 32 && bpp != 8) { set_status(status, status_cap, "BMP must be 8/24/32 bpp"); return -1; }
    uint32_t palette_entries = 0;
    if (bpp == 8) {
        palette_entries = rd32(data + 46);
        if (!palette_entries) palette_entries = 256;
        if (14u + dib + palette_entries * 4u > off || palette_entries > 256u) { set_status(status, status_cap, "invalid BMP palette"); return -1; }
    }
    uint32_t stride = (((uint32_t)w * bpp + 31u) / 32u) * 4u;
    if (off + (uint64_t)stride * (uint64_t)h > size) { set_status(status, status_cap, "truncated BMP"); return -1; }
    uint8_t *rgb = (uint8_t *)malloc((size_t)w * (size_t)h * 3u);
    if (!rgb) { set_status(status, status_cap, "out of memory"); return -1; }
    const uint8_t *pal = data + 14u + dib;
    for (int y = 0; y < h; ++y) {
        int sy = top_down ? y : (h - 1 - y);
        const uint8_t *row = data + off + (size_t)sy * stride;
        uint8_t *dst = rgb + ((size_t)y * (size_t)w * 3u);
        for (int x = 0; x < w; ++x) {
            if (bpp == 24) {
                dst[x * 3 + 0] = row[x * 3 + 2];
                dst[x * 3 + 1] = row[x * 3 + 1];
                dst[x * 3 + 2] = row[x * 3 + 0];
            } else if (bpp == 32) {
                dst[x * 3 + 0] = row[x * 4 + 2];
                dst[x * 3 + 1] = row[x * 4 + 1];
                dst[x * 3 + 2] = row[x * 4 + 0];
            } else {
                uint8_t pi = row[x];
                if (pi >= palette_entries) { dst[x * 3 + 0] = dst[x * 3 + 1] = dst[x * 3 + 2] = pi; }
                else { dst[x * 3 + 0] = pal[pi * 4 + 2]; dst[x * 3 + 1] = pal[pi * 4 + 1]; dst[x * 3 + 2] = pal[pi * 4 + 0]; }
            }
        }
    }
    out->w = w; out->h = h; out->rgb = rgb;
    return 1;
}

static int pnm_next_token(const uint8_t *data, size_t size, size_t *pos, char *tok, size_t tok_cap) {
    size_t i = *pos;
    while (i < size) {
        if (isspace((unsigned char)data[i])) { ++i; continue; }
        if (data[i] == '#') { while (i < size && data[i] != '\n') ++i; continue; }
        break;
    }
    if (i >= size) return 0;
    size_t n = 0;
    while (i < size && !isspace((unsigned char)data[i]) && data[i] != '#') {
        if (n + 1 < tok_cap) tok[n++] = (char)data[i];
        ++i;
    }
    tok[n] = 0;
    *pos = i;
    return n > 0;
}

static int load_pnm(const uint8_t *data, size_t size, bdm_rgb_image_t *out, char *status, size_t status_cap) {
    if (size < 2 || data[0] != 'P' || data[1] < '2' || data[1] > '6') return 0;
    int type = data[1] - '0';
    size_t pos = 2;
    char tok[64];
    if (!pnm_next_token(data, size, &pos, tok, sizeof(tok))) return -1;
    int w = atoi(tok);
    if (!pnm_next_token(data, size, &pos, tok, sizeof(tok))) return -1;
    int h = atoi(tok);
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) { set_status(status, status_cap, "invalid PNM dimensions"); return -1; }
    int maxv = 255;
    if (type != 1 && type != 4) {
        if (!pnm_next_token(data, size, &pos, tok, sizeof(tok))) return -1;
        maxv = atoi(tok);
        if (maxv <= 0 || maxv > 65535) { set_status(status, status_cap, "invalid PNM max value"); return -1; }
    }
    while (pos < size && isspace((unsigned char)data[pos])) ++pos;
    uint8_t *rgb = (uint8_t *)malloc((size_t)w * (size_t)h * 3u);
    if (!rgb) { set_status(status, status_cap, "out of memory"); return -1; }
    if (type == 5 || type == 6) {
        int channels = (type == 6) ? 3 : 1;
        int bytes = maxv > 255 ? 2 : 1;
        uint64_t need = (uint64_t)w * (uint64_t)h * (uint64_t)channels * (uint64_t)bytes;
        if (pos + need > size) { free(rgb); set_status(status, status_cap, "truncated PNM"); return -1; }
        const uint8_t *src = data + pos;
        for (int i = 0; i < w * h; ++i) {
            int vals[3];
            for (int c = 0; c < channels; ++c) {
                int v = bytes == 1 ? *src++ : ((int)src[0] << 8) | src[1];
                if (bytes == 2) src += 2;
                vals[c] = (v * 255 + maxv / 2) / maxv;
            }
            if (channels == 1) vals[1] = vals[2] = vals[0];
            rgb[i * 3 + 0] = (uint8_t)vals[0];
            rgb[i * 3 + 1] = (uint8_t)vals[1];
            rgb[i * 3 + 2] = (uint8_t)vals[2];
        }
    } else {
        for (int i = 0; i < w * h; ++i) {
            int vals[3] = { 0, 0, 0 };
            int channels = (type == 3) ? 3 : 1;
            for (int c = 0; c < channels; ++c) {
                if (!pnm_next_token(data, size, &pos, tok, sizeof(tok))) { free(rgb); set_status(status, status_cap, "truncated PNM"); return -1; }
                int v = atoi(tok);
                vals[c] = (type == 1) ? (v ? 0 : 255) : ((v * 255 + maxv / 2) / maxv);
            }
            if (channels == 1) vals[1] = vals[2] = vals[0];
            rgb[i * 3 + 0] = (uint8_t)vals[0];
            rgb[i * 3 + 1] = (uint8_t)vals[1];
            rgb[i * 3 + 2] = (uint8_t)vals[2];
        }
    }
    out->w = w; out->h = h; out->rgb = rgb;
    return 1;
}

static int load_image_file(const char *path, bdm_rgb_image_t *out, char *status, size_t status_cap) {
    size_t sz = 0;
    uint8_t *data = (uint8_t *)bdm_fe_read_file(path, &sz);
    if (!data) { set_status(status, status_cap, "image open failed"); return -1; }
    memset(out, 0, sizeof(*out));
    int rc = load_bmp(data, sz, out, status, status_cap);
    if (rc == 0) rc = load_pnm(data, sz, out, status, status_cap);
    free(data);
    if (rc == 0) { set_status(status, status_cap, "unsupported image; use BMP or PPM/PGM/PNM for native backends"); return -1; }
    return rc > 0 ? 0 : -1;
}

static uint8_t *scale_and_dither(const uint8_t *rgb, int sw, int sh, int stride, int dw, int dh, float strength) {
    if (!rgb || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return NULL;
    double *cur = (double *)calloc((size_t)(dw + 2), sizeof(double));
    double *next = (double *)calloc((size_t)(dw + 2), sizeof(double));
    uint8_t *bits = (uint8_t *)calloc((size_t)dw * (size_t)dh, 1u);
    if (!cur || !next || !bits) { free(cur); free(next); free(bits); return NULL; }
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;

    for (int y = 0; y < dh; ++y) {
        memset(next, 0, (size_t)(dw + 2) * sizeof(double));
        for (int x = 0; x < dw; ++x) {
            int sx0 = (int)((int64_t)x * sw / dw);
            int sx1 = (int)((int64_t)(x + 1) * sw / dw);
            int sy0 = (int)((int64_t)y * sh / dh);
            int sy1 = (int)((int64_t)(y + 1) * sh / dh);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sy1 <= sy0) sy1 = sy0 + 1;
            if (sx1 > sw) sx1 = sw;
            if (sy1 > sh) sy1 = sh;
            uint64_t sum = 0, count = 0;
            for (int yy = sy0; yy < sy1; ++yy) {
                const uint8_t *row = rgb + (size_t)yy * (size_t)stride;
                for (int xx = sx0; xx < sx1; ++xx) {
                    uint8_t r = row[xx * 3 + 0], g = row[xx * 3 + 1], b = row[xx * 3 + 2];
                    sum += (uint64_t)(77u * r + 150u * g + 29u * b) >> 8;
                    ++count;
                }
            }
            double oldv = (double)(sum / (count ? count : 1)) + cur[x + 1];
            if (oldv < 0.0) oldv = 0.0;
            if (oldv > 255.0) oldv = 255.0;
            uint8_t black = oldv < 128.0 ? 1u : 0u;
            bits[(size_t)y * (size_t)dw + (size_t)x] = black;
            double newv = black ? 0.0 : 255.0;
            double err = (oldv - newv) * (double)strength;
            cur[x + 2] += err * 7.0 / 16.0;
            next[x] += err * 3.0 / 16.0;
            next[x + 1] += err * 5.0 / 16.0;
            next[x + 2] += err * 1.0 / 16.0;
        }
        double *tmp = cur; cur = next; next = tmp;
    }
    free(cur);
    free(next);
    return bits;
}

static bdm_status_t run_steps(bdm_core_t *core, uint64_t steps) {
    return bdm_fe_run_checked(core, steps);
}

static int inject_bitmap_runs(const uint8_t *bits, int bw, int bh, const bdm_fe_rect_t *rect,
                              bdm_video_t *video, bdm_input_t *input, bdm_core_t *core,
                              const bdm_fe_image_import_options_t *opt, int *out_runs, int *out_pixels) {
    unsigned down_steps = opt && opt->down_steps ? opt->down_steps : 8000u;
    unsigned pixel_steps = opt && opt->pixel_steps ? opt->pixel_steps : 1600u;
    unsigned up_steps = opt && opt->up_steps ? opt->up_steps : 4000u;
    int ox = opt ? opt->touch_offset_x : 0;
    int oy = opt ? opt->touch_offset_y : 0;
    int runs = 0, pixels = 0;
    if (!bits || !rect || !video || !input || !core) return -1;

    for (int y = 0; y < bh; ++y) {
        int x = 0;
        while (x < bw) {
            while (x < bw && !bits[(size_t)y * (size_t)bw + (size_t)x]) ++x;
            if (x >= bw) break;
            int x0 = x;
            while (x < bw && bits[(size_t)y * (size_t)bw + (size_t)x]) ++x;
            int x1 = x - 1;
            ++runs;
            pixels += x1 - x0 + 1;
            for (int xx = x0; xx <= x1; ++xx) {
                float lx = (float)(rect->x + xx) + 0.5f;
                float ly = (float)(rect->y + y) + 0.5f;
                int32_t xfp = 0, yfp = 0;
                bdm_fe_logical_to_pen_fp(video, lx, ly, ox, oy, &xfp, &yfp);
                bdm_input_set_pen_fp(input, xfp, yfp, 1);
                bdm_status_t rc = run_steps(core, xx == x0 ? down_steps : pixel_steps);
                if (rc != BDM_OK) { bdm_input_set_pen_fp(input, xfp, yfp, 0); return -1; }
            }
            {
                float lx = (float)(rect->x + x1) + 0.5f;
                float ly = (float)(rect->y + y) + 0.5f;
                int32_t xfp = 0, yfp = 0;
                bdm_fe_logical_to_pen_fp(video, lx, ly, ox, oy, &xfp, &yfp);
                bdm_input_set_pen_fp(input, xfp, yfp, 0);
                if (run_steps(core, up_steps) != BDM_OK) return -1;
            }
        }
    }
    if (out_runs) *out_runs = runs;
    if (out_pixels) *out_pixels = pixels;
    return 0;
}

int bdm_fe_import_image_pixels_to_drawing_area(const uint8_t *rgb, int width, int height, int stride_bytes,
                                               bdm_video_t *video, bdm_input_t *input, bdm_core_t *core,
                                               const bdm_fe_image_import_options_t *opt,
                                               char *status, size_t status_cap) {
    if (!rgb || width <= 0 || height <= 0 || stride_bytes < width * 3 || !video || !input || !core) {
        set_status(status, status_cap, "invalid image import arguments");
        return -1;
    }
    bdm_fe_image_import_options_t local;
    if (opt) local = *opt;
    else bdm_fe_image_import_options_init(&local);
    bdm_fe_rect_t rect;
    if (bdm_fe_detect_drawing_area(video, &rect, status, status_cap) != 0) return -1;
    uint8_t *bits = scale_and_dither(rgb, width, height, stride_bytes, rect.w, rect.h, local.dither_strength);
    if (!bits) { set_status(status, status_cap, "image conversion failed"); return -1; }
    int runs = 0, pixels = 0;
    int rc = inject_bitmap_runs(bits, rect.w, rect.h, &rect, video, input, core, &local, &runs, &pixels);
    free(bits);
    if (rc != 0) { set_status(status, status_cap, "image injection stopped by CPU error"); return -1; }
    char buf[160];
    snprintf(buf, sizeof(buf), "imported image into %d,%d %dx%d; %d black pixels in %d pen runs", rect.x, rect.y, rect.w, rect.h, pixels, runs);
    set_status(status, status_cap, buf);
    return 0;
}

int bdm_fe_import_image_file_to_drawing_area(const char *path, bdm_video_t *video, bdm_input_t *input, bdm_core_t *core,
                                             const bdm_fe_image_import_options_t *opt, char *status, size_t status_cap) {
    bdm_rgb_image_t img;
    memset(&img, 0, sizeof(img));
    if (!path || !*path) { set_status(status, status_cap, "no image path"); return -1; }
    if (load_image_file(path, &img, status, status_cap) != 0) return -1;
    int rc = bdm_fe_import_image_pixels_to_drawing_area(img.rgb, img.w, img.h, img.w * 3, video, input, core, opt, status, status_cap);
    free(img.rgb);
    return rc;
}
