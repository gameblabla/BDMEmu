#include "bdm_frontend.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint64_t parse_u64(const char *s, uint64_t def) {
    if (!s || !*s) return def;
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 0);
    if (errno || !end || *end) return def;
    return (uint64_t)v;
}

static int parse_script_tap_arg(const char *s, bdm_fe_scripted_tap_t *tap) {
    if (!s || !tap) return 0;
    char *end = NULL;
    long px = strtol(s, &end, 0);
    if (!end || *end != ',') return 0;
    long py = strtol(end + 1, &end, 0);
    uint64_t down = 1000000ull;
    uint64_t up = 50000000ull;
    if (end && *end == ',') {
        char *tail = NULL;
        down = strtoull(end + 1, &tail, 0);
        if (!tail || *tail != ',') return 0;
        up = strtoull(tail + 1, &tail, 0);
        if (tail && *tail) return 0;
    } else if (end && *end) return 0;
    tap->x = (int)px;
    tap->y = (int)py;
    tap->down_steps = down;
    tap->up_steps = up;
    return 1;
}

void bdm_fe_options_init(bdm_fe_options_t *opt) {
    if (!opt) return;
    memset(opt, 0, sizeof(*opt));
    opt->scale = BDM_FE_DEFAULT_SCALE;
    opt->fps = BDM_FE_DEFAULT_FPS;
    opt->steps_per_second = BDM_FE_DEFAULT_STEPS_PER_SECOND;
    opt->sample_rate = BDM_FE_DEFAULT_SAMPLE_RATE;
    opt->touch_hold_ms = BDM_FE_DEFAULT_TOUCH_HOLD_MS;
    opt->calibration_touch_hold_ms = BDM_FE_DEFAULT_CALIBRATION_TOUCH_HOLD_MS;
    opt->state_slot_path = "bdm_state.bdmst";
    opt->enable_audio = 1;
    opt->auto_calibrate = 1;
    opt->integer_scaling = 1;
}

void bdm_fe_print_usage(FILE *f, const char *argv0, const char *frontend_name, int show_backend_options) {
    if (!f) f = stderr;
    if (!argv0) argv0 = "bdm";
    if (!frontend_name) frontend_name = "frontend";
    fprintf(f,
        "Usage: %s game_g.bin [game_m.bin] [--bios h8.bin]\n"
        "          [--scale N] [--fullscreen] [--fps N] [--steps-per-second N]\n"
        "          [--sample-rate HZ] [--no-audio] [--dump-wav file.wav]\n"
        "          [--touch-hold-ms N] [--calibration-touch-hold-ms N]\n"
        "          [--touch-offset-x N] [--touch-offset-y N] [--touch-debug]\n"
        "          [--auto-calibrate] [--no-auto-calibrate]\n"
        "          [--load-sram file.bin] [--save-sram file.bin]\n"
        "          [--load-state file.bdmst] [--save-state file.bdmst] [--state file.bdmst]\n"
        "          [--integer-scale] [--aspect-scale]\n"
        "          [--auto-title] [--auto-menu] [--auto-mode1]\n"
        "          [--tap-after-auto X,Y[,DOWN,UP]]\n",
        argv0);
    if (show_backend_options) {
        fprintf(f,
            "          [--video gdi|d3d11] [--audio waveout|wasapi|none]\n");
    }
    fprintf(f,
        "\n%s input defaults:\n"
        "  Pen:         mouse/touch on the 160x120 LCD; short clicks are held for ADC sampling\n"
        "  Menu A-E:    A/B/C/D/E (Z/X are alternate host keys for A/B)\n"
        "  Page L/R:    Left/Right, Backspace/Return, or [/]\n"
        "  Reset:       R\n"
        "  Save/load:   F5 / F8, using --state or bdm_state.bdmst\n"
        "  Scaling:     F9 toggles integer scale vs aspect-fit letterbox when supported\n"
        "  Fullscreen:  F11 toggles fullscreen when supported\n"
        "  Quit:        Escape or window close\n",
        frontend_name);
}

int bdm_fe_parse_args(int argc, char **argv, bdm_fe_options_t *opt, int allow_backend_options) {
    if (!opt) return 0;
    bdm_fe_options_init(opt);
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--cart") && i + 1 < argc) opt->cart_path = argv[++i];
        else if (!strcmp(argv[i], "--media") && i + 1 < argc) opt->media_path = argv[++i];
        else if (!strcmp(argv[i], "--bios") && i + 1 < argc) opt->bios_path = argv[++i];
        else if (!strcmp(argv[i], "--load-sram") && i + 1 < argc) opt->load_sram_path = argv[++i];
        else if (!strcmp(argv[i], "--save-sram") && i + 1 < argc) opt->save_sram_path = argv[++i];
        else if ((!strcmp(argv[i], "--load-state") || !strcmp(argv[i], "--load_state")) && i + 1 < argc) opt->load_state_path = argv[++i];
        else if ((!strcmp(argv[i], "--save-state") || !strcmp(argv[i], "--save_state")) && i + 1 < argc) opt->save_state_path = argv[++i];
        else if (!strcmp(argv[i], "--state") && i + 1 < argc) opt->state_slot_path = argv[++i];
        else if (!strcmp(argv[i], "--dump-wav") && i + 1 < argc) opt->dump_wav_path = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) opt->scale = (unsigned)parse_u64(argv[++i], opt->scale);
        else if (!strcmp(argv[i], "--fps") && i + 1 < argc) opt->fps = (unsigned)parse_u64(argv[++i], opt->fps);
        else if (!strcmp(argv[i], "--steps-per-second") && i + 1 < argc) opt->steps_per_second = (unsigned)parse_u64(argv[++i], opt->steps_per_second);
        else if (!strcmp(argv[i], "--sample-rate") && i + 1 < argc) opt->sample_rate = (unsigned)parse_u64(argv[++i], opt->sample_rate);
        else if (!strcmp(argv[i], "--touch-hold-ms") && i + 1 < argc) opt->touch_hold_ms = (unsigned)parse_u64(argv[++i], opt->touch_hold_ms);
        else if (!strcmp(argv[i], "--calibration-touch-hold-ms") && i + 1 < argc) opt->calibration_touch_hold_ms = (unsigned)parse_u64(argv[++i], opt->calibration_touch_hold_ms);
        else if (!strcmp(argv[i], "--touch-offset-x") && i + 1 < argc) opt->touch_offset_x = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--touch-offset-y") && i + 1 < argc) opt->touch_offset_y = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--touch-debug")) opt->touch_debug = 1;
        else if (!strcmp(argv[i], "--fullscreen")) opt->fullscreen = 1;
        else if (!strcmp(argv[i], "--integer-scale")) opt->integer_scaling = 1;
        else if (!strcmp(argv[i], "--aspect-scale")) opt->integer_scaling = 0;
        else if (!strcmp(argv[i], "--no-audio")) opt->enable_audio = 0;
        else if (!strcmp(argv[i], "--auto-calibrate")) opt->auto_calibrate = 1;
        else if (!strcmp(argv[i], "--no-auto-calibrate")) opt->auto_calibrate = 0;
        else if (!strcmp(argv[i], "--auto-title")) opt->auto_title = 1;
        else if (!strcmp(argv[i], "--auto-menu")) opt->auto_menu = 1;
        else if (!strcmp(argv[i], "--auto-mode1")) { opt->auto_mode1 = 1; opt->auto_menu = 1; }
        else if (!strcmp(argv[i], "--tap-after-auto") && i + 1 < argc) {
            if (opt->post_auto_tap_count >= sizeof(opt->post_auto_taps) / sizeof(opt->post_auto_taps[0]) ||
                !parse_script_tap_arg(argv[++i], &opt->post_auto_taps[opt->post_auto_tap_count])) return 0;
            ++opt->post_auto_tap_count;
        }
        else if (allow_backend_options && !strcmp(argv[i], "--video") && i + 1 < argc) opt->video_backend = argv[++i];
        else if (allow_backend_options && !strcmp(argv[i], "--audio") && i + 1 < argc) opt->audio_backend = argv[++i];
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) return 0;
        else if (argv[i][0] != '-') {
            if (!opt->cart_path) opt->cart_path = argv[i];
            else if (!opt->media_path) opt->media_path = argv[i];
            else return 0;
        } else return 0;
    }

    if (!opt->cart_path && !opt->bios_path) return 0;
    if (opt->scale < 1u) opt->scale = 1u;
    if (opt->scale > 12u) opt->scale = 12u;
    if (opt->fps < 10u) opt->fps = 10u;
    if (opt->fps > 240u) opt->fps = 240u;
    if (opt->sample_rate < 8000u) opt->sample_rate = 8000u;
    if (opt->sample_rate > 192000u) opt->sample_rate = 192000u;
    if (opt->touch_hold_ms > 5000u) opt->touch_hold_ms = 5000u;
    if (opt->calibration_touch_hold_ms < opt->touch_hold_ms) opt->calibration_touch_hold_ms = opt->touch_hold_ms;
    if (opt->calibration_touch_hold_ms > 5000u) opt->calibration_touch_hold_ms = 5000u;
    if (opt->touch_offset_x < -8) opt->touch_offset_x = -8;
    if (opt->touch_offset_x > 8) opt->touch_offset_x = 8;
    if (opt->touch_offset_y < -8) opt->touch_offset_y = -8;
    if (opt->touch_offset_y > 8) opt->touch_offset_y = 8;
    if (opt->steps_per_second < 1000u) opt->steps_per_second = BDM_FE_DEFAULT_STEPS_PER_SECOND;
    return 1;
}

void *bdm_fe_read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "open failed: %s: %s\n", path ? path : "(null)", strerror(errno));
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

int bdm_fe_write_file_exact(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "open failed: %s: %s\n", path ? path : "(null)", strerror(errno));
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

int bdm_fe_dump_wav_samples(const char *path, const int16_t *samples, size_t frames, unsigned sample_rate) {
    if (!path) return -1;
    if (!samples && frames) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

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

int bdm_fe_dump_wav(const char *path, const bdm_sound_t *sound) {
    if (!path || !sound) return -1;
    size_t frames = 0;
    const int16_t *samples = bdm_sound_recorded_samples(sound, &frames);
    return bdm_fe_dump_wav_samples(path, samples, frames, bdm_sound_sample_rate(sound));
}

int bdm_fe_save_state_file(const char *path, bdm_core_t *core) {
    if (!path || !core) return 0;
    size_t need = bdm_core_state_size(core);
    void *buf = malloc(need ? need : 1u);
    if (!buf) return -1;
    size_t got = bdm_core_save_state(core, buf, need);
    int rc = (got == need) ? bdm_fe_write_file_exact(path, buf, need) : -1;
    free(buf);
    return rc;
}

int bdm_fe_load_state_file(const char *path, bdm_core_t *core) {
    if (!path || !core) return 0;
    size_t sz = 0;
    void *buf = bdm_fe_read_file(path, &sz);
    if (!buf) return -1;
    bdm_status_t rc = bdm_core_load_state(core, buf, sz);
    free(buf);
    return rc == BDM_OK ? 0 : -1;
}

int bdm_fe_save_sram_if_requested(const char *path, bdm_core_t *core) {
    if (!path || !core) return 0;
    size_t sram_size = bdm_core_external_sram_size(core);
    void *sram = malloc(sram_size ? sram_size : 1u);
    if (!sram) return -1;
    bdm_core_save_sram(core, sram, sram_size);
    int rc = bdm_fe_write_file_exact(path, sram, sram_size);
    free(sram);
    return rc;
}

int bdm_fe_machine_init(bdm_fe_machine_t *machine, const bdm_fe_options_t *opt) {
    if (!machine || !opt) return -1;
    memset(machine, 0, sizeof(*machine));
    machine->video = bdm_video_create();
    machine->input = bdm_input_create();
    machine->sound = bdm_sound_create();
    if (!machine->video || !machine->input || !machine->sound) return -1;
    bdm_sound_set_sample_rate(machine->sound, opt->sample_rate);
    bdm_sound_set_step_rate(machine->sound, opt->steps_per_second);
    bdm_sound_enable_recording(machine->sound, opt->dump_wav_path != NULL && !opt->enable_audio);
    bdm_core_config_t cfg;
    cfg.video = machine->video;
    cfg.input = machine->input;
    cfg.sound = machine->sound;
    machine->core = bdm_core_create(&cfg);
    if (!machine->core) return -1;

    if (opt->bios_path) {
        size_t sz = 0; void *data = bdm_fe_read_file(opt->bios_path, &sz);
        if (!data || bdm_core_load_bios(machine->core, data, sz) != BDM_OK) { free(data); fprintf(stderr, "bad BIOS image\n"); return -1; }
        free(data);
    }
    if (opt->cart_path) {
        size_t sz = 0; void *data = bdm_fe_read_file(opt->cart_path, &sz);
        if (!data || bdm_core_load_cart(machine->core, data, sz) != BDM_OK) { free(data); fprintf(stderr, "bad cart image\n"); return -1; }
        free(data);
    }
    if (opt->media_path) {
        size_t sz = 0; void *data = bdm_fe_read_file(opt->media_path, &sz);
        if (!data || bdm_core_load_media_cart(machine->core, data, sz) != BDM_OK) { free(data); fprintf(stderr, "bad media cart image\n"); return -1; }
        free(data);
    }
    bdm_core_reset(machine->core);
    if (opt->load_sram_path) {
        size_t sz = 0; void *data = bdm_fe_read_file(opt->load_sram_path, &sz);
        if (!data || bdm_core_load_sram(machine->core, data, sz) != BDM_OK) { free(data); fprintf(stderr, "bad SRAM image\n"); return -1; }
        free(data);
    }
    if (opt->load_state_path) {
        if (bdm_fe_load_state_file(opt->load_state_path, machine->core) != 0) { fprintf(stderr, "state load failed: %s\n", opt->load_state_path); return -1; }
        else printf("loaded %s\n", opt->load_state_path);
    }
    bdm_core_state_t initial_state;
    bdm_core_get_state(machine->core, &initial_state);
    printf("reset pc=%04x vector=%04x\n", initial_state.pc, bdm_core_bus_read16(machine->core, 0));

    int startup_auto_calibrate = opt->auto_calibrate && !opt->load_state_path;
    bdm_status_t auto_rc = bdm_fe_run_auto_sequence(machine->core, machine->input, startup_auto_calibrate,
                                                    startup_auto_calibrate ? opt->auto_title : 0,
                                                    startup_auto_calibrate ? opt->auto_menu : 0,
                                                    startup_auto_calibrate ? opt->auto_mode1 : 0);
    if (auto_rc != BDM_OK) {
        bdm_core_state_t st;
        bdm_core_get_state(machine->core, &st);
        fprintf(stderr, "auto sequence failed: rc=%d pc=%04x op=%04x steps=%" PRIu64 "\n", auto_rc, st.pc, st.last_opcode, st.steps);
    }
    for (size_t i = 0; i < opt->post_auto_tap_count; ++i) {
        const bdm_fe_scripted_tap_t *tap = &opt->post_auto_taps[i];
        bdm_status_t tap_rc = bdm_fe_scripted_tap(machine->core, machine->input, tap->x, tap->y, tap->down_steps, tap->up_steps);
        if (tap_rc != BDM_OK) {
            fprintf(stderr, "post-auto tap failed: rc=%d\n", tap_rc);
            break;
        }
    }
    return 0;
}

void bdm_fe_machine_destroy(bdm_fe_machine_t *machine) {
    if (!machine) return;
    bdm_core_destroy(machine->core);
    bdm_video_destroy(machine->video);
    bdm_input_destroy(machine->input);
    bdm_sound_destroy(machine->sound);
    memset(machine, 0, sizeof(*machine));
}


uint64_t bdm_fe_ms_to_steps(unsigned steps_per_second, unsigned ms) {
    if (steps_per_second == 0u) steps_per_second = BDM_FE_DEFAULT_STEPS_PER_SECOND;
    return ((uint64_t)steps_per_second * (uint64_t)ms + 999u) / 1000u;
}

bdm_status_t bdm_fe_run_checked(bdm_core_t *core, uint64_t count) {
    for (uint64_t i = 0; i < count; ++i) {
        bdm_status_t rc = bdm_core_step(core);
        if (rc != BDM_OK) return rc;
    }
    return BDM_OK;
}

uint64_t bdm_fe_core_steps_now(bdm_core_t *core) {
    bdm_core_state_t st;
    memset(&st, 0, sizeof(st));
    if (core) bdm_core_get_state(core, &st);
    return st.steps;
}

bdm_status_t bdm_fe_scripted_tap(bdm_core_t *core, bdm_input_t *input, int x, int y, uint64_t down_steps, uint64_t up_steps) {
    bdm_input_set_pen(input, x, y, true);
    bdm_status_t rc = bdm_fe_run_checked(core, down_steps);
    bdm_input_set_pen(input, x, y, false);
    if (rc != BDM_OK) return rc;
    return bdm_fe_run_checked(core, up_steps);
}

bdm_status_t bdm_fe_run_auto_sequence(bdm_core_t *core, bdm_input_t *input,
                                      int want_calibrate, int want_title, int want_menu, int want_mode1) {
    if (!want_calibrate && !want_title && !want_menu && !want_mode1) return BDM_OK;
    bdm_status_t rc = bdm_fe_run_checked(core, 5000000ull);
    if (rc != BDM_OK) return rc;
    rc = bdm_fe_scripted_tap(core, input, 10, 10, 1000000ull, 20000000ull);
    if (rc != BDM_OK) return rc;
    rc = bdm_fe_scripted_tap(core, input, 150, 110, 1000000ull, want_calibrate && !want_title && !want_menu ? 20000000ull : 50000000ull);
    if (rc != BDM_OK || (want_calibrate && !want_title && !want_menu)) return rc;
    rc = bdm_fe_scripted_tap(core, input, 48, 106, 1000000ull, 80000000ull);
    if (rc != BDM_OK) return rc;
    rc = bdm_fe_scripted_tap(core, input, 48, 106, 1000000ull, 80000000ull);
    if (rc != BDM_OK || (!want_menu && !want_mode1)) return rc;
    rc = bdm_fe_scripted_tap(core, input, 48, 106, 1000000ull, 80000000ull);
    if (rc != BDM_OK || !want_mode1) return rc;
    return bdm_fe_scripted_tap(core, input, 80, 50, 1000000ull, 100000000ull);
}


bdm_status_t bdm_fe_soft_reset(bdm_core_t *core, bdm_input_t *input, bdm_fe_touch_state_t *touch, const bdm_fe_options_t *opt, int allow_extended_auto) {
    if (!core || !input) return BDM_ERR_INVALID_ARGUMENT;
    bdm_core_reset(core);
    bdm_input_reset(input);
    if (touch) {
        int debug = touch->debug;
        memset(touch, 0, sizeof(*touch));
        touch->min_hold_steps = opt ? bdm_fe_ms_to_steps(opt->steps_per_second, opt->touch_hold_ms) : 0;
        touch->debug = debug;
    }
    if (!opt || !opt->auto_calibrate) return BDM_OK;
    return bdm_fe_run_auto_sequence(core, input, 1,
                                    allow_extended_auto ? opt->auto_title : 0,
                                    allow_extended_auto ? opt->auto_menu : 0,
                                    allow_extended_auto ? opt->auto_mode1 : 0);
}

int bdm_fe_framebuffer_pixel_on(uint32_t p) {
    uint8_t r = (uint8_t)(p >> 16);
    uint8_t g = (uint8_t)(p >> 8);
    uint8_t b = (uint8_t)p;
    return (unsigned)r + (unsigned)g + (unsigned)b < 220u;
}

int bdm_fe_video_has_calibration_target(const bdm_video_t *video) {
    size_t w = 0, h = 0;
    const uint32_t *fb = bdm_video_framebuffer(video, &w, &h);
    if (!fb || w < 160u || h < 120u) return 0;
    unsigned tl = 0, br = 0;
    for (unsigned y = 3; y < 20; ++y)
        for (unsigned x = 3; x < 20; ++x)
            if (bdm_fe_framebuffer_pixel_on(fb[y * w + x])) ++tl;
    for (unsigned y = 100; y < 120; ++y)
        for (unsigned x = 140; x < 160; ++x)
            if (bdm_fe_framebuffer_pixel_on(fb[y * w + x])) ++br;
    return tl >= 10u || br >= 10u;
}

static int32_t bdm_fe_float_to_fp(float v) {
    if (v <= -32768.0f) return (int32_t)-2147483647 - 1;
    if (v >= 32767.0f) return 2147418112;
    return (int32_t)(v * 65536.0f);
}

void bdm_fe_touch_prepare_down_for_video(bdm_fe_touch_state_t *touch, const bdm_video_t *video, uint64_t normal_hold_steps, uint64_t calibration_hold_steps) {
    if (!touch) return;
    if (normal_hold_steps == 0u) normal_hold_steps = 1u;
    if (calibration_hold_steps < normal_hold_steps) calibration_hold_steps = normal_hold_steps;
    touch->min_hold_steps = (video && bdm_fe_video_has_calibration_target(video)) ? calibration_hold_steps : normal_hold_steps;
}

void bdm_fe_logical_to_pen_fp(const bdm_video_t *video, float lx, float ly, int touch_offset_x, int touch_offset_y, int32_t *out_x_fp, int32_t *out_y_fp) {
    int32_t x_fp = bdm_fe_float_to_fp(lx);
    int32_t y_fp = bdm_fe_float_to_fp(ly);
    if (!bdm_fe_video_has_calibration_target(video)) {
        x_fp += (int32_t)touch_offset_x << 16;
        y_fp += (int32_t)touch_offset_y << 16;
    }
    const int32_t max_x = ((int32_t)BDM_LCD_ACTIVE_WIDTH << 16) - 1;
    const int32_t max_y = ((int32_t)BDM_LCD_ACTIVE_HEIGHT << 16) - 1;
    if (x_fp < 0) x_fp = 0;
    if (y_fp < 0) y_fp = 0;
    if (x_fp > max_x) x_fp = max_x;
    if (y_fp > max_y) y_fp = max_y;
    if (out_x_fp) *out_x_fp = x_fp;
    if (out_y_fp) *out_y_fp = y_fp;
}

void bdm_fe_logical_to_pen(const bdm_video_t *video, float lx, float ly, int touch_offset_x, int touch_offset_y, int *out_x, int *out_y) {
    int32_t x_fp = 0, y_fp = 0;
    bdm_fe_logical_to_pen_fp(video, lx, ly, touch_offset_x, touch_offset_y, &x_fp, &y_fp);
    if (out_x) *out_x = (int)(x_fp >> 16);
    if (out_y) *out_y = (int)(y_fp >> 16);
}

void bdm_fe_touch_apply_down_fp(bdm_input_t *input, bdm_fe_touch_state_t *touch, bdm_core_t *core, int32_t x_fp, int32_t y_fp) {
    if (!input || !touch) return;
    touch->x_fp = x_fp;
    touch->y_fp = y_fp;
    touch->x = (int)(x_fp >> 16);
    touch->y = (int)(y_fp >> 16);
    touch->physical_down = 1;
    touch->release_pending = 0;
    if (!touch->emulated_down) {
        touch->down_start_steps = bdm_fe_core_steps_now(core);
        touch->earliest_release_steps = touch->down_start_steps + touch->min_hold_steps;
        touch->emulated_down = 1;
        if (touch->debug) fprintf(stderr, "touch down %.3f,%.3f at step=%" PRIu64 " hold_until=%" PRIu64 "\n",
                                  (double)x_fp / 65536.0, (double)y_fp / 65536.0,
                                  touch->down_start_steps, touch->earliest_release_steps);
    }
    bdm_input_set_pen_fp(input, x_fp, y_fp, true);
}

void bdm_fe_touch_apply_down(bdm_input_t *input, bdm_fe_touch_state_t *touch, bdm_core_t *core, int x, int y) {
    bdm_fe_touch_apply_down_fp(input, touch, core, (int32_t)x << 16, (int32_t)y << 16);
}

void bdm_fe_touch_request_up_fp(bdm_input_t *input, bdm_fe_touch_state_t *touch, bdm_core_t *core, int32_t x_fp, int32_t y_fp) {
    (void)x_fp; (void)y_fp;
    if (!input || !touch) return;
    touch->physical_down = 0;
    uint64_t now = bdm_fe_core_steps_now(core);
    if (!touch->emulated_down) {
        bdm_input_set_pen_fp(input, touch->x_fp, touch->y_fp, false);
        return;
    }
    if (now >= touch->earliest_release_steps) {
        touch->emulated_down = 0;
        touch->release_pending = 0;
        bdm_input_set_pen_fp(input, touch->x_fp, touch->y_fp, false);
        if (touch->debug) fprintf(stderr, "touch up %.3f,%.3f at step=%" PRIu64 "\n",
                                  (double)touch->x_fp / 65536.0, (double)touch->y_fp / 65536.0, now);
    } else {
        touch->release_pending = 1;
        bdm_input_set_pen_fp(input, touch->x_fp, touch->y_fp, true);
        if (touch->debug) fprintf(stderr, "touch up deferred %.3f,%.3f at step=%" PRIu64 " until=%" PRIu64 "\n",
                                  (double)touch->x_fp / 65536.0, (double)touch->y_fp / 65536.0,
                                  now, touch->earliest_release_steps);
    }
}

void bdm_fe_touch_request_up(bdm_input_t *input, bdm_fe_touch_state_t *touch, bdm_core_t *core, int x, int y) {
    bdm_fe_touch_request_up_fp(input, touch, core, (int32_t)x << 16, (int32_t)y << 16);
}

void bdm_fe_touch_update_motion_fp(bdm_input_t *input, bdm_fe_touch_state_t *touch, int32_t x_fp, int32_t y_fp) {
    if (!input || !touch) return;
    touch->x_fp = x_fp;
    touch->y_fp = y_fp;
    touch->x = (int)(x_fp >> 16);
    touch->y = (int)(y_fp >> 16);
    if (touch->emulated_down) bdm_input_set_pen_fp(input, x_fp, y_fp, true);
}

void bdm_fe_touch_update_motion(bdm_input_t *input, bdm_fe_touch_state_t *touch, int x, int y) {
    bdm_fe_touch_update_motion_fp(input, touch, (int32_t)x << 16, (int32_t)y << 16);
}

void bdm_fe_touch_tick_release(bdm_input_t *input, bdm_fe_touch_state_t *touch, bdm_core_t *core) {
    if (!input || !touch || !touch->release_pending || touch->physical_down) return;
    uint64_t now = bdm_fe_core_steps_now(core);
    if (now >= touch->earliest_release_steps) {
        touch->emulated_down = 0;
        touch->release_pending = 0;
        bdm_input_set_pen_fp(input, touch->x_fp, touch->y_fp, false);
        if (touch->debug) fprintf(stderr, "touch deferred release %.3f,%.3f at step=%" PRIu64 "\n", (double)touch->x_fp / 65536.0, (double)touch->y_fp / 65536.0, now);
    }
}

void bdm_fe_touch_force_clear(bdm_input_t *input, bdm_fe_touch_state_t *touch) {
    if (!input || !touch) return;
    touch->physical_down = 0;
    touch->emulated_down = 0;
    touch->release_pending = 0;
    bdm_input_set_pen_fp(input, touch->x_fp, touch->y_fp, false);
}

int bdm_fe_panel_button_to_pen_fp(bdm_button_t button, int32_t *out_x_fp, int32_t *out_y_fp) {
    int x = 0, y = 0;
    switch (button) {
    case BDM_BUTTON_MENU_A: x = 16;  y = 4;  break;
    case BDM_BUTTON_MENU_B: x = 48;  y = 4;  break;
    case BDM_BUTTON_MENU_C: x = 80;  y = 4;  break;
    case BDM_BUTTON_MENU_D: x = 112; y = 4;  break;
    case BDM_BUTTON_MENU_E: x = 144; y = 4;  break;
    case BDM_BUTTON_PAGE_LEFT:  x = 3;   y = 60; break;
    case BDM_BUTTON_PAGE_RIGHT: x = 156; y = 60; break;
    default: return 0;
    }
    if (out_x_fp) *out_x_fp = ((int32_t)x << 16) | 0x8000;
    if (out_y_fp) *out_y_fp = ((int32_t)y << 16) | 0x8000;
    return 1;
}

void bdm_fe_set_panel_button(bdm_input_t *input, bdm_fe_touch_state_t *touch, bdm_core_t *core, bdm_button_t button, int pressed) {
    if (!input || button <= BDM_BUTTON_PEN || button >= BDM_BUTTON_COUNT) return;
    bdm_input_set_button(input, button, pressed != 0);

    /* The firmware decoder expects both a decoded panel command latch and the
       matching analog panel position.  The core mirrors the command latch from
       bdm_input_panel_code(); the frontend supplies the physical panel position
       for the same held button. */
    if (touch) {
        int32_t x_fp = 0, y_fp = 0;
        if (bdm_fe_panel_button_to_pen_fp(button, &x_fp, &y_fp)) {
            if (pressed) bdm_fe_touch_apply_down_fp(input, touch, core, x_fp, y_fp);
            else bdm_fe_touch_request_up_fp(input, touch, core, x_fp, y_fp);
        }
    }
}

static int fe_ascii_key_to_panel_button(unsigned key, bdm_button_t *out) {
    if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
    switch (key) {
    case 'A': case 'Z': if (out) *out = BDM_BUTTON_MENU_A; return 1;
    case 'B': case 'X': if (out) *out = BDM_BUTTON_MENU_B; return 1;
    case 'C': if (out) *out = BDM_BUTTON_MENU_C; return 1;
    case 'D': if (out) *out = BDM_BUTTON_MENU_D; return 1;
    case 'E': if (out) *out = BDM_BUTTON_MENU_E; return 1;
    case '[': case '<': if (out) *out = BDM_BUTTON_PAGE_LEFT; return 1;
    case ']': case '>': if (out) *out = BDM_BUTTON_PAGE_RIGHT; return 1;
    default: return 0;
    }
}

void bdm_fe_set_button_key_ascii(bdm_input_t *input, unsigned key, int pressed, int *quit_requested, bdm_core_t *core, bdm_fe_touch_state_t *touch) {
    if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
    bdm_button_t panel_button;
    if (fe_ascii_key_to_panel_button(key, &panel_button)) {
        bdm_fe_set_panel_button(input, touch, core, panel_button, pressed);
        return;
    }
    switch (key) {
    case 27u:
        if (pressed && quit_requested) *quit_requested = 1;
        break;
    case '\r':
    case '\n':
        bdm_fe_set_panel_button(input, touch, core, BDM_BUTTON_PAGE_RIGHT, pressed);
        break;
    case 8u:
        bdm_fe_set_panel_button(input, touch, core, BDM_BUTTON_PAGE_LEFT, pressed);
        break;
    case 'R':
        if (pressed && core) {
            bdm_core_reset(core);
            if (input) bdm_input_reset(input);
            if (touch) {
                uint64_t min_hold_steps = touch->min_hold_steps;
                int debug = touch->debug;
                memset(touch, 0, sizeof(*touch));
                touch->min_hold_steps = min_hold_steps;
                touch->debug = debug;
            }
        }
        break;
    default:
        break;
    }
}
