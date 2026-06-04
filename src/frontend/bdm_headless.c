#include "bdm_core.h"
#include "bdm_input.h"
#include "bdm_sound.h"
#include "bdm_video.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [game_g.bin [game_m.bin]] [--bios h8_328.bin] [--steps N]\n"
        "          [--trace N] [--port7 HEX] [--press NAME] [--pen X,Y[,down]]\n"
        "          [--auto-calibrate] [--auto-title] [--auto-menu] [--auto-mode1]\n"
        "          [--tap-after-auto X,Y[,DOWN,UP]]  repeatable scripted taps\n"
        "          [--drag-after-auto X0,Y0,X1,Y1[,POINT,UP]] scripted drag\n"
        "          [--dump-frame file.ppm] [--dump-lcd-ram file.bin] [--dump-wav file.wav]\n"
        "          [--load-sram file.bin] [--save-sram file.bin]\n"
        "          [--load-state file.bdmst] [--save-state file.bdmst]\n"
        "          [--sample-rate HZ] [--audio-step-rate HZ]\n"
        "          [--lcd-test-pattern] [--rom-preview OFFSET] [--rom-preview-media]\n"
        "\n"
        "Examples:\n"
        "  %s 'Dragon Ball Z Taisen-gata Search Battle [G.01] (Japan).bin' --steps 500000\n"
        "  %s game.bin --trace 128 --steps 128\n"
        "  %s game_g.bin game_m.bin --auto-title --dump-frame title.ppm\n"
        "  %s game.bin --rom-preview 0x11c00 --steps 0 --dump-frame preview.ppm\n",
        argv0, argv0, argv0, argv0, argv0);
}

static void *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "open failed: %s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    void *data = malloc((size_t)sz ? (size_t)sz : 1u);
    if (!data) { fclose(f); return NULL; }
    if (sz && fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "read failed: %s\n", path);
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (out_size) *out_size = (size_t)sz;
    return data;
}

static int write_file_exact(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "open failed: %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (size && fwrite(data, 1, size, f) != size) {
        fprintf(stderr, "write failed: %s\n", path);
        fclose(f);
        return -1;
    }
    int err = ferror(f);
    fclose(f);
    return err ? -1 : 0;
}


static int save_state_file(const char *path, bdm_core_t *core) {
    if (!path || !core) return 0;
    size_t need = bdm_core_state_size(core);
    void *buf = malloc(need ? need : 1u);
    if (!buf) return -1;
    size_t got = bdm_core_save_state(core, buf, need);
    int rc = (got == need) ? write_file_exact(path, buf, need) : -1;
    free(buf);
    return rc;
}

static int load_state_file(const char *path, bdm_core_t *core) {
    if (!path || !core) return 0;
    size_t sz = 0;
    void *buf = read_file(path, &sz);
    if (!buf) return -1;
    bdm_status_t rc = bdm_core_load_state(core, buf, sz);
    free(buf);
    return rc == BDM_OK ? 0 : -1;
}

static unsigned long long parse_u64(const char *s, unsigned long long def) {
    if (!s || !*s) return def;
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 0);
    if (errno || !end || *end) return def;
    return v;
}

static int parse_pen_arg(const char *s, int *x, int *y, int *down) {
    if (!s || !x || !y || !down) return 0;
    char *end = NULL;
    long px = strtol(s, &end, 0);
    if (!end || *end != ',') return 0;
    long py = strtol(end + 1, &end, 0);
    int pd = 1;
    if (end && *end == ',') {
        long v = strtol(end + 1, &end, 0);
        if (end && *end) return 0;
        pd = v != 0;
    } else if (end && *end) return 0;
    *x = (int)px;
    *y = (int)py;
    *down = pd;
    return 1;
}

typedef struct scripted_tap_event {
    int x;
    int y;
    unsigned long long down_steps;
    unsigned long long up_steps;
} scripted_tap_event_t;

typedef struct scripted_drag_event {
    int x0;
    int y0;
    int x1;
    int y1;
    unsigned long long point_steps;
    unsigned long long up_steps;
} scripted_drag_event_t;

static int parse_script_tap_arg(const char *s, scripted_tap_event_t *tap) {
    if (!s || !tap) return 0;
    char *end = NULL;
    long px = strtol(s, &end, 0);
    if (!end || *end != ',') return 0;
    long py = strtol(end + 1, &end, 0);
    unsigned long long down = 1000000ull;
    unsigned long long up = 50000000ull;
    if (end && *end == ',') {
        down = strtoull(end + 1, &end, 0);
        if (!end || *end != ',') return 0;
        up = strtoull(end + 1, &end, 0);
        if (end && *end) return 0;
    } else if (end && *end) return 0;
    tap->x = (int)px;
    tap->y = (int)py;
    tap->down_steps = down;
    tap->up_steps = up;
    return 1;
}

static int parse_script_drag_arg(const char *s, scripted_drag_event_t *drag) {
    if (!s || !drag) return 0;
    char *end = NULL;
    long x0 = strtol(s, &end, 0);
    if (!end || *end != ',') return 0;
    long y0 = strtol(end + 1, &end, 0);
    if (!end || *end != ',') return 0;
    long x1 = strtol(end + 1, &end, 0);
    if (!end || *end != ',') return 0;
    long y1 = strtol(end + 1, &end, 0);
    unsigned long long point_steps = 1000000ull;
    unsigned long long up_steps = 50000000ull;
    if (end && *end == ',') {
        point_steps = strtoull(end + 1, &end, 0);
        if (!end || *end != ',') return 0;
        up_steps = strtoull(end + 1, &end, 0);
        if (end && *end) return 0;
    } else if (end && *end) return 0;
    drag->x0 = (int)x0;
    drag->y0 = (int)y0;
    drag->x1 = (int)x1;
    drag->y1 = (int)y1;
    drag->point_steps = point_steps;
    drag->up_steps = up_steps;
    return 1;
}

static int parse_button(const char *s, bdm_button_t *out) {
    if (!s || !out) return 0;
    if (!strcmp(s, "pen")) *out = BDM_BUTTON_PEN;
    else if (!strcmp(s, "a") || !strcmp(s, "menu-a")) *out = BDM_BUTTON_MENU_A;
    else if (!strcmp(s, "b") || !strcmp(s, "menu-b")) *out = BDM_BUTTON_MENU_B;
    else if (!strcmp(s, "c") || !strcmp(s, "menu-c")) *out = BDM_BUTTON_MENU_C;
    else if (!strcmp(s, "d") || !strcmp(s, "menu-d")) *out = BDM_BUTTON_MENU_D;
    else if (!strcmp(s, "e") || !strcmp(s, "menu-e")) *out = BDM_BUTTON_MENU_E;
    else if (!strcmp(s, "left") || !strcmp(s, "page-left")) *out = BDM_BUTTON_PAGE_LEFT;
    else if (!strcmp(s, "right") || !strcmp(s, "page-right")) *out = BDM_BUTTON_PAGE_RIGHT;
    else return 0;
    return 1;
}


static void apply_input_state(bdm_input_t *input, int pen_set, int pen_x, int pen_y, int pen_down, const bool pressed[BDM_BUTTON_COUNT], int port7_set, unsigned port7) {
    if (!input) return;
    if (pen_set) bdm_input_set_pen(input, pen_x, pen_y, pen_down != 0);
    for (int bi = 0; bi < BDM_BUTTON_COUNT; ++bi) {
        if (pressed[bi]) bdm_input_set_button(input, (bdm_button_t)bi, true);
    }
    if (port7_set) bdm_input_set_port7_override(input, (uint8_t)port7);
}

static void print_state(const bdm_core_state_t *st) {
    printf("steps=%" PRIu64 " pc=%04x last=%04x op=%04x ccr=%02x "
           "r0=%04x r1=%04x r2=%04x r3=%04x r4=%04x r5=%04x r6=%04x r7=%04x\n",
           st->steps, st->pc, st->last_pc, st->last_opcode, st->ccr,
           st->r[0], st->r[1], st->r[2], st->r[3], st->r[4], st->r[5], st->r[6], st->r[7]);
}

static int dump_ppm(const char *path, bdm_video_t *video) {
    size_t w = 0, h = 0;
    const uint32_t *fb = bdm_video_framebuffer(video, &w, &h);
    if (!fb || !w || !h) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%zu %zu\n255\n", w, h);
    for (size_t i = 0; i < w * h; ++i) {
        unsigned char rgb[3];
        rgb[0] = (unsigned char)((fb[i] >> 16) & 0xffu);
        rgb[1] = (unsigned char)((fb[i] >> 8) & 0xffu);
        rgb[2] = (unsigned char)(fb[i] & 0xffu);
        fwrite(rgb, 1, sizeof(rgb), f);
    }
    fclose(f);
    return 0;
}

static int preview_rom_to_lcd(bdm_video_t *video, const uint8_t *data, size_t size, size_t offset) {
    if (!video || !data || offset >= size) return -1;

    bdm_video_lcd_index_write(video, 0x12u);
    bdm_video_lcd_data_write(video, (uint8_t)BDM_LCD_STRIDE_BYTES);
    bdm_video_lcd_index_write(video, 0x14u);
    bdm_video_lcd_data_write(video, (uint8_t)(BDM_LCD_ACTIVE_HEIGHT - 1u));

    for (size_t i = 0; i < BDM_LCD_VRAM_SIZE; ++i) {
        uint8_t v = (offset + i < size) ? data[offset + i] : 0x00u;
        bdm_video_lcd_vram_write(video, i, v);
    }
    return 0;
}

static int dump_wav(const char *path, const bdm_sound_t *sound) {
    if (!path || !sound) return -1;
    size_t frames = 0;
    const int16_t *samples = bdm_sound_recorded_samples(sound, &frames);
    if (!samples && frames) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    unsigned sample_rate = bdm_sound_sample_rate(sound);
    uint16_t channels = 1;
    uint16_t bits = 16;
    uint32_t data_bytes = (uint32_t)(frames * channels * (bits / 8u));
    uint32_t riff_size = 36u + data_bytes;
    uint32_t byte_rate = sample_rate * channels * (bits / 8u);
    uint16_t block_align = (uint16_t)(channels * (bits / 8u));

    #define W8(v) do { unsigned char _b = (unsigned char)(v); fwrite(&_b, 1, 1, f); } while (0)
    #define W16(v) do { uint16_t _x = (uint16_t)(v); W8(_x & 0xffu); W8((_x >> 8) & 0xffu); } while (0)
    #define W32(v) do { uint32_t _x = (uint32_t)(v); W8(_x & 0xffu); W8((_x >> 8) & 0xffu); W8((_x >> 16) & 0xffu); W8((_x >> 24) & 0xffu); } while (0)

    fwrite("RIFF", 1, 4, f); W32(riff_size); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); W32(16u); W16(1u); W16(channels); W32(sample_rate); W32(byte_rate); W16(block_align); W16(bits);
    fwrite("data", 1, 4, f); W32(data_bytes);
    for (size_t i = 0; i < frames; ++i) W16((uint16_t)samples[i]);

    #undef W8
    #undef W16
    #undef W32

    int err = ferror(f);
    fclose(f);
    return err ? -1 : 0;
}

static int dump_lcd_ram(const char *path, const bdm_core_t *core) {
    if (!path || !core) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    for (unsigned a = 0x8000u; a <= 0x895fu; ++a) {
        unsigned char v = bdm_core_bus_read8(core, (uint16_t)a);
        fwrite(&v, 1, 1, f);
    }
    fclose(f);
    return 0;
}

static bdm_status_t run_checked(bdm_core_t *core, unsigned long long count, unsigned long long trace) {
    for (unsigned long long i = 0; i < count; ++i) {
        if (trace && i < trace) {
            bdm_core_state_t st;
            bdm_core_get_state(core, &st);
            printf("trace[%llu] pc=%04x op=%04x ccr=%02x r0=%04x r1=%04x r2=%04x r3=%04x r4=%04x r5=%04x r6=%04x r7=%04x\n",
                   i, st.pc, bdm_core_bus_read16(core, st.pc), st.ccr,
                   st.r[0], st.r[1], st.r[2], st.r[3], st.r[4], st.r[5], st.r[6], st.r[7]);
        }
        bdm_status_t rc = bdm_core_step(core);
        if (rc != BDM_OK) return rc;
    }
    return BDM_OK;
}

static bdm_status_t scripted_tap(bdm_core_t *core, bdm_input_t *input,
                                 int x, int y,
                                 unsigned long long down_steps,
                                 unsigned long long up_steps) {
    bdm_input_set_pen(input, x, y, true);
    bdm_status_t rc = run_checked(core, down_steps, 0);
    bdm_input_set_pen(input, x, y, false);
    if (rc != BDM_OK) return rc;
    return run_checked(core, up_steps, 0);
}

static int iabs_int(int v) { return v < 0 ? -v : v; }

static bdm_status_t scripted_drag(bdm_core_t *core, bdm_input_t *input,
                                  int x0, int y0, int x1, int y1,
                                  unsigned long long point_steps,
                                  unsigned long long up_steps) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int n = iabs_int(dx) > iabs_int(dy) ? iabs_int(dx) : iabs_int(dy);
    if (n < 1) n = 1;
    const int substeps = 8;
    unsigned long long sub_step_count = point_steps / (unsigned long long)substeps;
    if (sub_step_count == 0ull) sub_step_count = 1ull;
    for (int i = 0; i <= n; ++i) {
        for (int s = 0; s < substeps; ++s) {
            int denom = n * substeps;
            int pos = i * substeps + s;
            if (pos > denom) pos = denom;
            int32_t x_fp = ((int32_t)x0 << 16) + (int32_t)(((int64_t)dx * (int64_t)pos * 65536) / (int64_t)denom);
            int32_t y_fp = ((int32_t)y0 << 16) + (int32_t)(((int64_t)dy * (int64_t)pos * 65536) / (int64_t)denom);
            bdm_input_set_pen_fp(input, x_fp, y_fp, true);
            bdm_status_t rc = run_checked(core, sub_step_count, 0);
            if (rc != BDM_OK) {
                bdm_input_set_pen_fp(input, x_fp, y_fp, false);
                return rc;
            }
        }
    }
    bdm_input_set_pen(input, x1, y1, false);
    return run_checked(core, up_steps, 0);
}

static bdm_status_t run_auto_sequence(bdm_core_t *core, bdm_input_t *input,
                                      int want_calibrate, int want_title, int want_menu, int want_mode1) {
    if (!want_calibrate && !want_title && !want_menu && !want_mode1) return BDM_OK;

    /* The supplied G carts first enter the common touch-panel calibration and
       later show two media-destructive-use confirmation pages.  Drive the
       same events a user would perform: top-left cross, bottom-right cross,
       then the on-screen yes button.  Coordinates are LCD pixels. */
    bdm_status_t rc = run_checked(core, 5000000ull, 0);
    if (rc != BDM_OK) return rc;

    rc = scripted_tap(core, input, 10, 10, 1000000ull, 20000000ull);
    if (rc != BDM_OK) return rc;
    rc = scripted_tap(core, input, 150, 110, 1000000ull, want_calibrate && !want_title && !want_menu ? 20000000ull : 50000000ull);
    if (rc != BDM_OK || (want_calibrate && !want_title && !want_menu)) return rc;

    rc = scripted_tap(core, input, 48, 106, 1000000ull, 80000000ull);
    if (rc != BDM_OK) return rc;
    rc = scripted_tap(core, input, 48, 106, 1000000ull, 80000000ull);
    if (rc != BDM_OK || (!want_menu && !want_mode1)) return rc;

    /* The title logo waits for another stylus/button confirmation before the
       game enters its mode-selection screen. */
    rc = scripted_tap(core, input, 48, 106, 1000000ull, 80000000ull);
    if (rc != BDM_OK || !want_mode1) return rc;

    /* First item in the DBZ mode menu.  This is a normal frontend input script,
       not a game-specific CPU/VRAM patch. */
    return scripted_tap(core, input, 80, 50, 1000000ull, 100000000ull);
}

int main(int argc, char **argv) {
    const char *cart_path = NULL;
    const char *media_path = NULL;
    const char *bios_path = NULL;
    const char *dump_frame = NULL;
    const char *dump_lcd_path = NULL;
    const char *dump_wav_path = NULL;
    const char *load_sram_path = NULL;
    const char *save_sram_path = NULL;
    const char *load_state_path = NULL;
    const char *save_state_path = NULL;
    unsigned sample_rate = 44100u;
    unsigned audio_step_rate = 2000000u;
    unsigned long long steps = 500000ull;
    unsigned long long trace = 0ull;
    unsigned long long rom_preview_offset = 0ull;
    int steps_set = 0;
    int port7_set = 0;
    int lcd_test_pattern = 0;
    int rom_preview = 0;
    int rom_preview_media = 0;
    int auto_calibrate = 0;
    int auto_title = 0;
    int auto_menu = 0;
    int auto_mode1 = 0;
    int pen_set = 0;
    int pen_x = 0;
    int pen_y = 0;
    int pen_down = 0;
    bool pressed[BDM_BUTTON_COUNT] = { false };
    scripted_tap_event_t post_auto_taps[64];
    scripted_drag_event_t post_auto_drags[64];
    size_t post_auto_tap_count = 0;
    size_t post_auto_drag_count = 0;
    unsigned port7 = 0xffu;
    void *cart_data = NULL;
    size_t cart_size = 0;
    void *media_data = NULL;
    size_t media_size = 0;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--cart") && i + 1 < argc) cart_path = argv[++i];
        else if (!strcmp(argv[i], "--media") && i + 1 < argc) media_path = argv[++i];
        else if (!strcmp(argv[i], "--bios") && i + 1 < argc) bios_path = argv[++i];
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) { steps = parse_u64(argv[++i], steps); steps_set = 1; }
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) trace = parse_u64(argv[++i], trace);
        else if (!strcmp(argv[i], "--port7") && i + 1 < argc) { port7 = (unsigned)parse_u64(argv[++i], 0xffu); port7_set = 1; }
        else if (!strcmp(argv[i], "--press") && i + 1 < argc) {
            bdm_button_t b;
            if (!parse_button(argv[++i], &b)) { usage(argv[0]); return 2; }
            pressed[b] = true;
        }
        else if (!strcmp(argv[i], "--pen") && i + 1 < argc) {
            if (!parse_pen_arg(argv[++i], &pen_x, &pen_y, &pen_down)) { usage(argv[0]); return 2; }
            pen_set = 1;
        }
        else if ((!strcmp(argv[i], "--dump-frame") || !strcmp(argv[i], "--dump_frame")) && i + 1 < argc) dump_frame = argv[++i];
        else if (!strcmp(argv[i], "--dump-frame.ppm")) dump_frame = "dump-frame.ppm";
        else if ((!strncmp(argv[i], "--dump-frame=", 13))) dump_frame = argv[i] + 13;
        else if ((!strcmp(argv[i], "--dump-lcd-ram") || !strcmp(argv[i], "--dump_lcd_ram")) && i + 1 < argc) dump_lcd_path = argv[++i];
        else if ((!strcmp(argv[i], "--dump-wav") || !strcmp(argv[i], "--dump_wav")) && i + 1 < argc) dump_wav_path = argv[++i];
        else if ((!strcmp(argv[i], "--load-sram") || !strcmp(argv[i], "--load_sram")) && i + 1 < argc) load_sram_path = argv[++i];
        else if ((!strcmp(argv[i], "--save-sram") || !strcmp(argv[i], "--save_sram")) && i + 1 < argc) save_sram_path = argv[++i];
        else if ((!strcmp(argv[i], "--load-state") || !strcmp(argv[i], "--load_state")) && i + 1 < argc) load_state_path = argv[++i];
        else if ((!strcmp(argv[i], "--save-state") || !strcmp(argv[i], "--save_state")) && i + 1 < argc) save_state_path = argv[++i];
        else if (!strcmp(argv[i], "--sample-rate") && i + 1 < argc) sample_rate = (unsigned)parse_u64(argv[++i], sample_rate);
        else if (!strcmp(argv[i], "--audio-step-rate") && i + 1 < argc) audio_step_rate = (unsigned)parse_u64(argv[++i], audio_step_rate);
        else if (!strcmp(argv[i], "--lcd-test-pattern")) lcd_test_pattern = 1;
        else if (!strcmp(argv[i], "--rom-preview") && i + 1 < argc) { rom_preview_offset = parse_u64(argv[++i], 0ull); rom_preview = 1; }
        else if (!strcmp(argv[i], "--rom-preview-media")) rom_preview_media = 1;
        else if (!strcmp(argv[i], "--auto-calibrate")) auto_calibrate = 1;
        else if (!strcmp(argv[i], "--auto-title")) auto_title = 1;
        else if (!strcmp(argv[i], "--auto-menu")) auto_menu = 1;
        else if (!strcmp(argv[i], "--auto-mode1")) { auto_mode1 = 1; auto_menu = 1; }
        else if (!strcmp(argv[i], "--tap-after-auto") && i + 1 < argc) {
            if (post_auto_tap_count >= sizeof(post_auto_taps) / sizeof(post_auto_taps[0]) ||
                !parse_script_tap_arg(argv[++i], &post_auto_taps[post_auto_tap_count])) { usage(argv[0]); return 2; }
            ++post_auto_tap_count;
        }
        else if (!strcmp(argv[i], "--drag-after-auto") && i + 1 < argc) {
            if (post_auto_drag_count >= sizeof(post_auto_drags) / sizeof(post_auto_drags[0]) ||
                !parse_script_drag_arg(argv[++i], &post_auto_drags[post_auto_drag_count])) { usage(argv[0]); return 2; }
            ++post_auto_drag_count;
        }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(argv[0]); return 0; }
        else if (argv[i][0] != '-') {
            if (!cart_path) cart_path = argv[i];
            else if (!media_path) media_path = argv[i];
            else { usage(argv[0]); return 2; }
        }
        else { usage(argv[0]); return 2; }
    }

    if (!cart_path && !media_path && !bios_path && !lcd_test_pattern && !rom_preview) {
        usage(argv[0]);
        return 2;
    }
    if ((lcd_test_pattern || rom_preview) && !steps_set && !cart_path && !bios_path) steps = 0;
    if (rom_preview && !steps_set) steps = 0;
    if ((auto_calibrate || auto_title || auto_menu || auto_mode1) && !steps_set) steps = 0;

    bdm_video_t *video = bdm_video_create();
    bdm_input_t *input = bdm_input_create();
    bdm_sound_t *sound = bdm_sound_create();
    if (sound) {
        bdm_sound_set_sample_rate(sound, sample_rate);
        bdm_sound_set_step_rate(sound, audio_step_rate);
        bdm_sound_enable_recording(sound, dump_wav_path != NULL);
    }
    if (!video || !input || !sound) {
        fprintf(stderr, "allocation failed\n");
        return 1;
    }
    apply_input_state(input, pen_set, pen_x, pen_y, pen_down, pressed, port7_set, port7);

    bdm_core_config_t cfg;
    cfg.video = video;
    cfg.input = input;
    cfg.sound = sound;
    bdm_core_t *core = bdm_core_create(&cfg);
    if (!core) {
        fprintf(stderr, "core allocation failed\n");
        return 1;
    }

    if (bios_path) {
        size_t sz = 0;
        void *data = read_file(bios_path, &sz);
        if (!data) return 1;
        bdm_status_t st = bdm_core_load_bios(core, data, sz);
        free(data);
        if (st != BDM_OK) { fprintf(stderr, "bad BIOS image\n"); return 1; }
    }
    if (cart_path) {
        cart_data = read_file(cart_path, &cart_size);
        if (!cart_data) return 1;
        bdm_status_t st = bdm_core_load_cart(core, cart_data, cart_size);
        if (st != BDM_OK) { fprintf(stderr, "bad cart image\n"); return 1; }
    }
    if (media_path) {
        media_data = read_file(media_path, &media_size);
        if (!media_data) return 1;
        bdm_status_t st = bdm_core_load_media_cart(core, media_data, media_size);
        if (st != BDM_OK) { fprintf(stderr, "bad media cart image\n"); return 1; }
    }

    bdm_core_reset(core);
    if (load_sram_path) {
        size_t sram_size = 0;
        void *sram_data = read_file(load_sram_path, &sram_size);
        if (!sram_data) return 1;
        bdm_status_t st_sram = bdm_core_load_sram(core, sram_data, sram_size);
        free(sram_data);
        if (st_sram != BDM_OK) { fprintf(stderr, "bad SRAM image\n"); return 1; }
    }
    apply_input_state(input, pen_set, pen_x, pen_y, pen_down, pressed, port7_set, port7);
    if (load_state_path) {
        if (load_state_file(load_state_path, core) != 0) { fprintf(stderr, "state load failed: %s\n", load_state_path); return 1; }
        else printf("loaded %s\n", load_state_path);
    }

    bdm_core_state_t st;
    bdm_core_get_state(core, &st);
    printf("reset pc=%04x vector=%04x\n", st.pc, bdm_core_bus_read16(core, 0));

    if (lcd_test_pattern) {
        bdm_video_lcd_test_pattern(video);
    }
    if (rom_preview) {
        const uint8_t *preview_data = rom_preview_media ? (const uint8_t *)media_data : (const uint8_t *)cart_data;
        size_t preview_size = rom_preview_media ? media_size : cart_size;
        if (preview_rom_to_lcd(video, preview_data, preview_size, (size_t)rom_preview_offset) != 0) {
            fprintf(stderr, "ROM preview failed: source=%s offset=0x%llx\n",
                    rom_preview_media ? "media" : "cart", rom_preview_offset);
            return 1;
        }
    }

    bdm_status_t auto_rc = run_auto_sequence(core, input, auto_calibrate, auto_title, auto_menu, auto_mode1);
    if (auto_rc != BDM_OK) {
        bdm_core_get_state(core, &st);
        if (auto_rc == BDM_ERR_UNSUPPORTED_OPCODE) {
            fprintf(stderr, "unsupported opcode at %04x: %04x after %" PRIu64 " steps\n",
                    st.unsupported_pc, st.unsupported_opcode, st.steps);
        } else {
            fprintf(stderr, "emulation error during auto input sequence\n");
        }
    }

    for (size_t ti = 0; ti < post_auto_tap_count; ++ti) {
        bdm_status_t tap_rc = scripted_tap(core, input, post_auto_taps[ti].x, post_auto_taps[ti].y,
                                           post_auto_taps[ti].down_steps, post_auto_taps[ti].up_steps);
        if (tap_rc != BDM_OK) {
            bdm_core_get_state(core, &st);
            if (tap_rc == BDM_ERR_UNSUPPORTED_OPCODE) {
                fprintf(stderr, "unsupported opcode at %04x: %04x after %" PRIu64 " steps\n",
                        st.unsupported_pc, st.unsupported_opcode, st.steps);
            } else {
                fprintf(stderr, "emulation error during post-auto tap sequence\n");
            }
            break;
        }
    }

    for (size_t di = 0; di < post_auto_drag_count; ++di) {
        bdm_status_t drag_rc = scripted_drag(core, input, post_auto_drags[di].x0, post_auto_drags[di].y0,
                                             post_auto_drags[di].x1, post_auto_drags[di].y1,
                                             post_auto_drags[di].point_steps, post_auto_drags[di].up_steps);
        if (drag_rc != BDM_OK) {
            bdm_core_get_state(core, &st);
            if (drag_rc == BDM_ERR_UNSUPPORTED_OPCODE) {
                fprintf(stderr, "unsupported opcode at %04x: %04x after %" PRIu64 " steps\n",
                        st.unsupported_pc, st.unsupported_opcode, st.steps);
            } else {
                fprintf(stderr, "emulation error during post-auto drag sequence\n");
            }
            break;
        }
    }

    bdm_status_t run_rc = run_checked(core, steps, trace);
    if (run_rc != BDM_OK) {
        bdm_core_get_state(core, &st);
        if (run_rc == BDM_ERR_UNSUPPORTED_OPCODE) {
            fprintf(stderr, "unsupported opcode at %04x: %04x after %" PRIu64 " steps\n",
                    st.unsupported_pc, st.unsupported_opcode, st.steps);
        } else {
            fprintf(stderr, "emulation error\n");
        }
    }

    bdm_video_present_headless(video);
    bdm_core_get_state(core, &st);
    print_state(&st);
    printf("lcd=%zux%zu stride=%zu dirty=%" PRIu64 " reg12=%02x reg14=%02x bank=%u media=%u:%u\n",
           bdm_video_lcd_active_width(video), bdm_video_lcd_active_height(video),
           bdm_video_lcd_stride_bytes(video), bdm_video_lcd_dirty_count(video),
           bdm_video_lcd_reg(video, 0x12), bdm_video_lcd_reg(video, 0x14),
           st.cart_bank, st.media_selected ? 1u : 0u, st.media_bank);
    printf("hw=t16:%04x/%04x tier=%02x tsr=%02x tcr=%02x adc=%02x/%02x p6=%02x p7=%02x\n",
           st.timer16_counter, st.timer16_compare, st.timer16_tier, st.timer16_tsr,
           st.timer16_tcr, st.adc_status, st.adc_control, st.panel_drive, st.port7_value);
    printf("audio=%zu frames rate=%u step_rate=%u events=%" PRIu64 " timer0_tcsr=%02x\n",
           bdm_sound_recorded_frames(sound), bdm_sound_sample_rate(sound),
           bdm_sound_step_rate(sound), bdm_sound_event_count(sound),
           bdm_sound_last_timer0_tcsr(sound));
    if (st.unsupported) return 3;

    if (dump_frame) {
        if (dump_ppm(dump_frame, video) != 0) fprintf(stderr, "frame dump failed: %s\n", dump_frame);
        else printf("wrote %s\n", dump_frame);
    }
    if (dump_wav_path) {
        if (dump_wav(dump_wav_path, sound) != 0) fprintf(stderr, "WAV dump failed: %s\n", dump_wav_path);
        else printf("wrote %s\n", dump_wav_path);
    }
    if (save_state_path) {
        if (save_state_file(save_state_path, core) != 0) fprintf(stderr, "state save failed: %s\n", save_state_path);
        else printf("wrote %s\n", save_state_path);
    }

    if (save_sram_path) {
        size_t sram_size = bdm_core_external_sram_size(core);
        void *sram_data = malloc(sram_size ? sram_size : 1u);
        if (!sram_data) fprintf(stderr, "SRAM allocation failed\n");
        else {
            bdm_core_save_sram(core, sram_data, sram_size);
            if (write_file_exact(save_sram_path, sram_data, sram_size) != 0) fprintf(stderr, "SRAM save failed: %s\n", save_sram_path);
            else printf("wrote %s\n", save_sram_path);
            free(sram_data);
        }
    }

    if (!dump_lcd_path) dump_lcd_path = getenv("BDM_DUMP_RAM");
    if (dump_lcd_path) {
        if (dump_lcd_ram(dump_lcd_path, core) != 0) fprintf(stderr, "LCD RAM dump failed: %s\n", dump_lcd_path);
        else printf("wrote %s\n", dump_lcd_path);
    }

    free(cart_data);
    free(media_data);
    bdm_core_destroy(core);
    bdm_video_destroy(video);
    bdm_input_destroy(input);
    bdm_sound_destroy(sound);
    return 0;
}
