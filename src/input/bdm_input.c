#include "bdm_input.h"

#include <stdlib.h>
#include <string.h>

struct bdm_input {
    bool buttons[BDM_BUTTON_COUNT];
    int pen_x;
    int pen_y;
    int32_t pen_x_fp;
    int32_t pen_y_fp;
    bool pen_down;
    bool port7_override_enabled;
    uint8_t port7_override;
};

bdm_input_t *bdm_input_create(void) {
    bdm_input_t *in = (bdm_input_t *)calloc(1, sizeof(*in));
    if (in) bdm_input_reset(in);
    return in;
}

void bdm_input_destroy(bdm_input_t *input) {
    free(input);
}

void bdm_input_reset(bdm_input_t *input) {
    if (!input) return;
    memset(input->buttons, 0, sizeof(input->buttons));
    input->pen_x = 0;
    input->pen_y = 0;
    input->pen_x_fp = 0;
    input->pen_y_fp = 0;
    input->pen_down = false;
    input->port7_override_enabled = false;
    input->port7_override = 0xff;
}

void bdm_input_set_button(bdm_input_t *input, bdm_button_t button, bool pressed) {
    if (!input || button < 0 || button >= BDM_BUTTON_COUNT) return;
    input->buttons[button] = pressed;
}

void bdm_input_set_pen(bdm_input_t *input, int x, int y, bool down) {
    if (!input) return;
    bdm_input_set_pen_fp(input, (int32_t)x << 16, (int32_t)y << 16, down);
}

void bdm_input_set_pen_fp(bdm_input_t *input, int32_t x_fp, int32_t y_fp, bool down) {
    if (!input) return;
    input->pen_x_fp = x_fp;
    input->pen_y_fp = y_fp;
    input->pen_x = (int)(x_fp >> 16);
    input->pen_y = (int)(y_fp >> 16);
    input->pen_down = down;
    input->buttons[BDM_BUTTON_PEN] = down;
}

void bdm_input_set_port7_override(bdm_input_t *input, uint8_t value) {
    if (!input) return;
    input->port7_override_enabled = true;
    input->port7_override = value;
}

void bdm_input_clear_port7_override(bdm_input_t *input) {
    if (!input) return;
    input->port7_override_enabled = false;
}

int bdm_input_pen_x(const bdm_input_t *input) {
    return input ? input->pen_x : 0;
}

int bdm_input_pen_y(const bdm_input_t *input) {
    return input ? input->pen_y : 0;
}

int32_t bdm_input_pen_x_fp(const bdm_input_t *input) {
    return input ? input->pen_x_fp : 0;
}

int32_t bdm_input_pen_y_fp(const bdm_input_t *input) {
    return input ? input->pen_y_fp : 0;
}

bool bdm_input_pen_down(const bdm_input_t *input) {
    return input ? input->pen_down : false;
}

uint8_t bdm_input_read_port7(const bdm_input_t *input) {
    if (!input) return 0xff;
    if (input->port7_override_enabled) return input->port7_override;

    /* The A/B/C/D/E and left/right controls on the Design Master are
       touch-panel/menu controls, not conventional controller action bits.
       Frontends synthesize stylus taps at the documented panel locations for
       those controls.  P7 is therefore kept to the observed ready/sense role
       and the pen contact latch only. */
    uint8_t v = 0xef;
    v |= 0x80u;                    /* gate-array ready; bit 4 is the active-low battery/sense line */
    if (input->pen_down || input->buttons[BDM_BUTTON_PEN]) {
        v &= (uint8_t)~0x40u;      /* pen/contact sense, tentative */
        v &= (uint8_t)~0x10u;      /* pen edge/debounce, tentative */
    }
    return v;
}

#define BDM_INPUT_STATE_VERSION 3u
#define BDM_INPUT_STATE_SIZE (4u + 4u + BDM_BUTTON_COUNT + 4u + 4u + 1u + 1u + 1u)

static void in_w32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t in_r32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

size_t bdm_input_state_size(const bdm_input_t *input) {
    (void)input;
    return BDM_INPUT_STATE_SIZE;
}

size_t bdm_input_save_state(const bdm_input_t *input, void *out_data, size_t out_capacity) {
    if (!input) return 0u;
    size_t need = BDM_INPUT_STATE_SIZE;
    if (!out_data || out_capacity < need) return need;
    uint8_t *p = (uint8_t *)out_data;
    memcpy(p, "BDMI", 4); p += 4;
    in_w32(p, BDM_INPUT_STATE_VERSION); p += 4;
    for (unsigned i = 0; i < BDM_BUTTON_COUNT; ++i) *p++ = input->buttons[i] ? 1u : 0u;
    in_w32(p, (uint32_t)input->pen_x_fp); p += 4;
    in_w32(p, (uint32_t)input->pen_y_fp); p += 4;
    *p++ = input->pen_down ? 1u : 0u;
    /* Do not serialize the debug P7 override as hardware/input state. */
    *p++ = 0u;
    *p++ = 0xffu;
    return need;
}

int bdm_input_load_state(bdm_input_t *input, const void *data, size_t size) {
    if (!input || !data || size != BDM_INPUT_STATE_SIZE) return -1;
    const uint8_t *p = (const uint8_t *)data;
    if (memcmp(p, "BDMI", 4) != 0) return -1;
    p += 4;
    uint32_t version = in_r32(p);
    if (version != BDM_INPUT_STATE_VERSION) return -1;
    p += 4;
    memset(input->buttons, 0, sizeof(input->buttons));
    for (unsigned i = 0; i < (unsigned)BDM_BUTTON_COUNT; ++i) input->buttons[i] = (*p++ != 0u);
    input->pen_x_fp = (int32_t)in_r32(p); p += 4;
    input->pen_y_fp = (int32_t)in_r32(p); p += 4;
    input->pen_x = (int)(input->pen_x_fp >> 16);
    input->pen_y = (int)(input->pen_y_fp >> 16);
    input->pen_down = (*p++ != 0u);
    input->buttons[BDM_BUTTON_PEN] = input->pen_down;
    /* Port 7 override is debug/headless injection, not machine state. */
    (void)*p++;
    (void)*p++;
    input->port7_override_enabled = false;
    input->port7_override = 0xffu;
    return 0;
}
